#include "RBluetooth.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>

#define RFCOMM_MIN_CHANNEL 1
#define RFCOMM_MAX_CHANNEL 30

// BlueZ D-Bus 이름
#define BLUEZ_SERVICE "org.bluez"
#define BLUEZ_AGENT_INTERFACE "org.bluez.Agent1"
#define BLUEZ_AGENT_MANAGER_INTERFACE "org.bluez.AgentManager1"
#define BLUEZ_ADAPTER_INTERFACE "org.bluez.Adapter1"
#define BLUEZ_DEVICE_INTERFACE "org.bluez.Device1"
#define BLUEZ_AGENT_PATH "/com/iotcontrol/BluetoothAgent"
#define BLUEZ_PATH_SIZE 128
#define BLUEZ_DISCOVERY_POLL_MS 200

// 페어링 중 BlueZ가 PIN을 물어보면 답하는 에이전트 상태
typedef struct _PairAgent
{
    char pin[BLUETOOTH_PIN_SIZE + 1];
} PairAgent;

// 비동기 Device1.Pair 결과
typedef struct _PairResult
{
    int finished;
    int error; // 0: 성공, 그 외 errno
} PairResult;

// 검색 결과를 모으는 상태
typedef struct _ScanList
{
    const char *nameFilter;
    BluetoothScanDevice *devices;
    size_t maxDevices;
    size_t count;
} ScanList;

static int DiscoverRfcommChannel(const bdaddr_t *bluetoothAddress);
static int SearchRfcommChannel(sdp_session_t *session);
static int ReadRfcommChannel(sdp_list_t *responseList);
static uint64_t GetMonotonicMs(void);
static int ReplyAgentEmpty(sd_bus_message *message, void *userData, sd_bus_error *error);
static int ReplyAgentPinCode(sd_bus_message *message, void *userData, sd_bus_error *error);
static int ReplyAgentPasskey(sd_bus_message *message, void *userData, sd_bus_error *error);
static int PairWithAgent(sd_bus *bus, PairAgent *agent, const char *bluetoothMac, int timeoutSeconds);
static int RegisterPairAgent(sd_bus *bus);
static void UnregisterPairAgent(sd_bus *bus);
static int ReadDeviceAddressMatches(sd_bus_message *reply, const char *bluetoothMac);
static int ReadObjectInterfaces(sd_bus_message *reply, const char *objectPath, const char *bluetoothMac, char adapterPath[BLUEZ_PATH_SIZE]);
static int ReadManagedObjects(sd_bus_message *reply, const char *bluetoothMac, char devicePath[BLUEZ_PATH_SIZE], char adapterPath[BLUEZ_PATH_SIZE]);
static int FindDevicePath(sd_bus *bus, const char *bluetoothMac, char devicePath[BLUEZ_PATH_SIZE], char adapterPath[BLUEZ_PATH_SIZE]);
static int DiscoverDevice(sd_bus *bus, const char *bluetoothMac, int timeoutSeconds, char devicePath[BLUEZ_PATH_SIZE]);
static int ReceivePairReply(sd_bus_message *reply, void *userData, sd_bus_error *error);
static int PairDevice(sd_bus *bus, const char *devicePath, int timeoutSeconds);
static int ScanWithDiscovery(sd_bus *bus, ScanList *scanList, int scanSeconds);
static int ReadScanObjects(sd_bus_message *reply, ScanList *scanList);
static int ReadScanInterfaces(sd_bus_message *reply, ScanList *scanList);
static int ReadScanDevice(sd_bus_message *reply, ScanList *scanList);
static int MatchesNameFilter(const char *name, const char *nameFilter);

// org.bluez.Agent1: PIN/패스키 요청에만 답하고 나머지는 빈 응답
static const sd_bus_vtable PAIR_AGENT_VTABLE[] =
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPinCode", "o", "s", ReplyAgentPinCode, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPasskey", "o", "u", ReplyAgentPasskey, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPinCode", "os", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestAuthorization", "o", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AuthorizeService", "os", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", "", "", ReplyAgentEmpty, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int ConnectBluetoothDevice(const char *bluetoothMac, int timeoutMs, uint8_t *rfcommChannel)
{
    struct sockaddr_rc bluetoothAddress;
    struct pollfd socketEvent;
    socklen_t socketErrorSize = sizeof(int);
    int discoveredChannel;
    int bluetoothFd;
    int socketFlags;
    int socketError = 0;
    int connectResult;

    if(bluetoothMac == NULL || timeoutMs <= 0 || rfcommChannel == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    memset(&bluetoothAddress, 0, sizeof(bluetoothAddress));
    bluetoothAddress.rc_family = AF_BLUETOOTH;
    if(str2ba(bluetoothMac, &bluetoothAddress.rc_bdaddr) != 0)
    {
        errno = EINVAL;
        return -1;
    }

    discoveredChannel = DiscoverRfcommChannel(&bluetoothAddress.rc_bdaddr);
    if(discoveredChannel < RFCOMM_MIN_CHANNEL || discoveredChannel > RFCOMM_MAX_CHANNEL)
    {
        return -1;
    }
    bluetoothAddress.rc_channel = (uint8_t)discoveredChannel;

    bluetoothFd = socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, BTPROTO_RFCOMM);
    if(bluetoothFd < 0)
    {
        return -1;
    }

    socketFlags = fcntl(bluetoothFd, F_GETFL, 0);
    if(socketFlags < 0 || fcntl(bluetoothFd, F_SETFL, socketFlags | O_NONBLOCK) < 0)
    {
        close(bluetoothFd);
        return -1;
    }

    connectResult = connect(bluetoothFd, (struct sockaddr *)&bluetoothAddress, sizeof(bluetoothAddress));
    if(connectResult < 0 && errno != EINPROGRESS)
    {
        close(bluetoothFd);
        return -1;
    }

    if(connectResult < 0)
    {
        socketEvent.fd = bluetoothFd;
        socketEvent.events = POLLOUT;
        socketEvent.revents = 0;

        do
        {
            connectResult = poll(&socketEvent, 1, timeoutMs);
        }
        while(connectResult < 0 && errno == EINTR);

        if(connectResult <= 0)
        {
            if(connectResult == 0)
            {
                errno = ETIMEDOUT;
            }
            close(bluetoothFd);
            return -1;
        }

        if(getsockopt(bluetoothFd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorSize) < 0 || socketError != 0)
        {
            if(socketError != 0)
            {
                errno = socketError;
            }
            close(bluetoothFd);
            return -1;
        }
    }

    if(fcntl(bluetoothFd, F_SETFL, socketFlags) < 0)
    {
        close(bluetoothFd);
        return -1;
    }

    *rfcommChannel = (uint8_t)discoveredChannel;
    return bluetoothFd;
}

int PairBluetoothDevice(const char *bluetoothMac, const char *pin, int timeoutSeconds)
{
    PairAgent agent = {{0}};
    sd_bus *bus = NULL;
    bdaddr_t bluetoothAddress;
    size_t pinLength;
    int busResult;

    if(bluetoothMac == NULL || pin == NULL || timeoutSeconds <= 0 || str2ba(bluetoothMac, &bluetoothAddress) != 0)
    {
        errno = EINVAL;
        return -1;
    }
    pinLength = strnlen(pin, BLUETOOTH_PIN_SIZE + 1);
    if(pinLength == 0 || pinLength > BLUETOOTH_PIN_SIZE || strspn(pin, "0123456789") != pinLength)
    {
        errno = EINVAL;
        return -1;
    }
    memcpy(agent.pin, pin, pinLength);

    busResult = sd_bus_open_system(&bus);
    if(busResult >= 0)
    {
        busResult = PairWithAgent(bus, &agent, bluetoothMac, timeoutSeconds);
    }
    sd_bus_flush_close_unref(bus);
    explicit_bzero(&agent, sizeof(agent));
    if(busResult < 0)
    {
        errno = -busResult;
        return -1;
    }
    return 0;
}

int ScanBluetoothDevices(const char *nameFilter, int scanSeconds, BluetoothScanDevice *devices, size_t maxDevices)
{
    ScanList scanList = {nameFilter, devices, maxDevices, 0};
    sd_bus *bus = NULL;
    int busResult;

    if(devices == NULL || maxDevices == 0 || scanSeconds <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    busResult = sd_bus_open_system(&bus);
    if(busResult >= 0)
    {
        busResult = ScanWithDiscovery(bus, &scanList, scanSeconds);
    }
    sd_bus_flush_close_unref(bus);
    if(busResult < 0)
    {
        errno = -busResult;
        return -1;
    }
    return (int)scanList.count;
}

// SDP로 장치의 Serial Port 서비스 RFCOMM 채널을 찾음. 실패하면 -1 (errno 설정)
static int DiscoverRfcommChannel(const bdaddr_t *bluetoothAddress)
{
    sdp_session_t *session;
    int rfcommChannel;

    session = sdp_connect(BDADDR_ANY, bluetoothAddress, SDP_RETRY_IF_BUSY);
    if(session == NULL)
    {
        return -1;
    }
    rfcommChannel = SearchRfcommChannel(session);
    sdp_close(session);
    if(rfcommChannel < 0)
    {
        errno = -rfcommChannel;
        return -1;
    }
    return rfcommChannel;
}

// 채널 번호 또는 -errno
static int SearchRfcommChannel(sdp_session_t *session)
{
    sdp_list_t *searchList;
    sdp_list_t *attributeList;
    sdp_list_t *responseList = NULL;
    uuid_t serialPortUuid;
    uint32_t attributeRange = 0x0000ffff;
    int result = -ENOMEM;

    sdp_uuid16_create(&serialPortUuid, SERIAL_PORT_SVCLASS_ID);
    searchList = sdp_list_append(NULL, &serialPortUuid);
    attributeList = sdp_list_append(NULL, &attributeRange);
    if(searchList != NULL && attributeList != NULL)
    {
        if(sdp_service_search_attr_req(session, searchList, SDP_ATTR_REQ_RANGE, attributeList, &responseList) < 0)
        {
            result = errno != 0 ? -errno : -EIO;
        }
        else
        {
            result = ReadRfcommChannel(responseList);
        }
    }
    for(sdp_list_t *response = responseList; response != NULL; response = response->next)
    {
        sdp_record_free((sdp_record_t *)response->data);
    }
    sdp_list_free(responseList, NULL);
    sdp_list_free(attributeList, NULL);
    sdp_list_free(searchList, NULL);
    return result;
}

// SDP 응답 레코드 중 첫 번째 유효한 RFCOMM 채널. 없으면 -ENOENT
static int ReadRfcommChannel(sdp_list_t *responseList)
{
    for(sdp_list_t *response = responseList; response != NULL; response = response->next)
    {
        sdp_list_t *protocols = NULL;
        int channel;

        if(sdp_get_access_protos((sdp_record_t *)response->data, &protocols) != 0)
        {
            continue;
        }
        channel = sdp_get_proto_port(protocols, RFCOMM_UUID);
        sdp_list_foreach(protocols, (sdp_list_func_t)sdp_list_free, NULL);
        sdp_list_free(protocols, NULL);
        if(channel >= RFCOMM_MIN_CHANNEL && channel <= RFCOMM_MAX_CHANNEL)
        {
            return channel;
        }
    }
    return -ENOENT;
}

static uint64_t GetMonotonicMs(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int ReplyAgentEmpty(sd_bus_message *message, void *userData, sd_bus_error *error)
{
    (void)userData;
    (void)error;
    return sd_bus_reply_method_return(message, "");
}

static int ReplyAgentPinCode(sd_bus_message *message, void *userData, sd_bus_error *error)
{
    const PairAgent *agent = (const PairAgent *)userData;

    (void)error;
    return sd_bus_reply_method_return(message, "s", agent->pin);
}

static int ReplyAgentPasskey(sd_bus_message *message, void *userData, sd_bus_error *error)
{
    const PairAgent *agent = (const PairAgent *)userData;

    (void)error;
    // PIN은 PairBluetoothDevice에서 숫자만 허용함
    return sd_bus_reply_method_return(message, "u", (uint32_t)strtoul(agent->pin, NULL, 10));
}

// 에이전트를 등록한 동안 장치를 찾아 페어링. 0 또는 -errno
static int PairWithAgent(sd_bus *bus, PairAgent *agent, const char *bluetoothMac, int timeoutSeconds)
{
    sd_bus_slot *agentSlot = NULL;
    char devicePath[BLUEZ_PATH_SIZE];
    int busResult;

    busResult = sd_bus_add_object_vtable(bus, &agentSlot, BLUEZ_AGENT_PATH, BLUEZ_AGENT_INTERFACE, PAIR_AGENT_VTABLE, agent);
    if(busResult >= 0)
    {
        busResult = RegisterPairAgent(bus);
        if(busResult >= 0)
        {
            busResult = DiscoverDevice(bus, bluetoothMac, timeoutSeconds, devicePath);
            if(busResult >= 0)
            {
                busResult = PairDevice(bus, devicePath, timeoutSeconds);
            }
            UnregisterPairAgent(bus);
        }
    }
    sd_bus_slot_unref(agentSlot);
    return busResult;
}

// Pair를 호출한 연결의 에이전트가 PIN 요청을 받음
static int RegisterPairAgent(sd_bus *bus)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int busResult = sd_bus_call_method(bus, BLUEZ_SERVICE, "/org/bluez", BLUEZ_AGENT_MANAGER_INTERFACE, "RegisterAgent", &error, NULL, "os", BLUEZ_AGENT_PATH, "KeyboardOnly");

    sd_bus_error_free(&error);
    return busResult;
}

static void UnregisterPairAgent(sd_bus *bus)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;

    sd_bus_call_method(bus, BLUEZ_SERVICE, "/org/bluez", BLUEZ_AGENT_MANAGER_INTERFACE, "UnregisterAgent", &error, NULL, "o", BLUEZ_AGENT_PATH);
    sd_bus_error_free(&error);
}

// Device1 속성(a{sv})을 읽고 Address가 bluetoothMac이면 1
static int ReadDeviceAddressMatches(sd_bus_message *reply, const char *bluetoothMac)
{
    int matches = 0;
    int busResult;

    busResult = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if(busResult < 0)
    {
        return busResult;
    }
    while((busResult = sd_bus_message_enter_container(reply, 'e', "sv")) > 0)
    {
        const char *name;
        const char *address;

        busResult = sd_bus_message_read(reply, "s", &name);
        if(busResult >= 0)
        {
            if(strcmp(name, "Address") == 0)
            {
                busResult = sd_bus_message_read(reply, "v", "s", &address);
                if(busResult >= 0 && strcasecmp(address, bluetoothMac) == 0)
                {
                    matches = 1;
                }
            }
            else
            {
                busResult = sd_bus_message_skip(reply, "v");
            }
        }
        if(busResult < 0 || (busResult = sd_bus_message_exit_container(reply)) < 0)
        {
            return busResult;
        }
    }
    if(busResult < 0 || (busResult = sd_bus_message_exit_container(reply)) < 0)
    {
        return busResult;
    }
    return matches;
}

// 객체 하나의 인터페이스 목록(a{sa{sv}})을 읽음. Device1의 Address가 bluetoothMac이면 1
// Adapter1이면 adapterPath가 비어 있을 때 objectPath를 기록
static int ReadObjectInterfaces(sd_bus_message *reply, const char *objectPath, const char *bluetoothMac, char adapterPath[BLUEZ_PATH_SIZE])
{
    int matches = 0;
    int busResult = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");

    while(busResult >= 0 && (busResult = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0)
    {
        const char *interfaceName;

        busResult = sd_bus_message_read(reply, "s", &interfaceName);
        if(busResult >= 0 && strcmp(interfaceName, BLUEZ_DEVICE_INTERFACE) == 0)
        {
            busResult = ReadDeviceAddressMatches(reply, bluetoothMac);
            matches |= busResult > 0;
        }
        else if(busResult >= 0)
        {
            if(strcmp(interfaceName, BLUEZ_ADAPTER_INTERFACE) == 0 && adapterPath[0] == '\0' && strlen(objectPath) < BLUEZ_PATH_SIZE)
            {
                strcpy(adapterPath, objectPath);
            }
            busResult = sd_bus_message_skip(reply, "a{sv}");
        }
        if(busResult >= 0)
        {
            busResult = sd_bus_message_exit_container(reply);
        }
    }
    if(busResult >= 0)
    {
        busResult = sd_bus_message_exit_container(reply);
    }
    return busResult < 0 ? busResult : matches;
}

// GetManagedObjects 응답(a{oa{sa{sv}}}: 객체 경로 → 인터페이스 → 속성)에서 장치 경로를 찾음
// 1: 찾음, 0: 없음, 음수: -errno
static int ReadManagedObjects(sd_bus_message *reply, const char *bluetoothMac, char devicePath[BLUEZ_PATH_SIZE], char adapterPath[BLUEZ_PATH_SIZE])
{
    int found = 0;
    int busResult = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");

    while(busResult >= 0 && (busResult = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0)
    {
        const char *objectPath;

        busResult = sd_bus_message_read(reply, "o", &objectPath);
        if(busResult >= 0)
        {
            busResult = ReadObjectInterfaces(reply, objectPath, bluetoothMac, adapterPath);
        }
        if(busResult > 0 && !found && strlen(objectPath) < BLUEZ_PATH_SIZE)
        {
            strcpy(devicePath, objectPath);
            found = 1;
        }
        if(busResult >= 0)
        {
            busResult = sd_bus_message_exit_container(reply);
        }
    }
    if(busResult >= 0)
    {
        busResult = sd_bus_message_exit_container(reply);
    }
    return busResult < 0 ? busResult : found;
}

// BlueZ 객체 목록에서 장치 경로를 찾음. 1: 찾음, 0: 없음, 음수: -errno
// adapterPath에는 처음 보이는 어댑터 경로를 기록 (없으면 빈 문자열)
static int FindDevicePath(sd_bus *bus, const char *bluetoothMac, char devicePath[BLUEZ_PATH_SIZE], char adapterPath[BLUEZ_PATH_SIZE])
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int busResult;

    adapterPath[0] = '\0';
    busResult = sd_bus_call_method(bus, BLUEZ_SERVICE, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", &error, &reply, "");
    if(busResult >= 0)
    {
        busResult = ReadManagedObjects(reply, bluetoothMac, devicePath, adapterPath);
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return busResult;
}

// 이미 알려진 장치면 바로 반환, 아니면 검색을 켜고 timeoutSeconds 동안 장치가 나타나길 기다림
static int DiscoverDevice(sd_bus *bus, const char *bluetoothMac, int timeoutSeconds, char devicePath[BLUEZ_PATH_SIZE])
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char adapterPath[BLUEZ_PATH_SIZE];
    struct timespec pollInterval = {0, BLUEZ_DISCOVERY_POLL_MS * 1000000L};
    uint64_t deadline;
    int discoveryStarted = 0;
    int busResult;

    busResult = FindDevicePath(bus, bluetoothMac, devicePath, adapterPath);
    if(busResult != 0)
    {
        return busResult > 0 ? 0 : busResult;
    }
    if(adapterPath[0] == '\0')
    {
        return -ENODEV;
    }

    busResult = sd_bus_call_method(bus, BLUEZ_SERVICE, adapterPath, BLUEZ_ADAPTER_INTERFACE, "StartDiscovery", &error, NULL, "");
    if(busResult >= 0)
    {
        discoveryStarted = 1;
    }
    else if(!sd_bus_error_has_name(&error, "org.bluez.Error.InProgress"))
    {
        sd_bus_error_free(&error);
        return busResult;
    }
    sd_bus_error_free(&error);

    busResult = 0;
    deadline = GetMonotonicMs() + (uint64_t)timeoutSeconds * 1000U;
    while(GetMonotonicMs() < deadline)
    {
        busResult = FindDevicePath(bus, bluetoothMac, devicePath, adapterPath);
        if(busResult != 0)
        {
            break;
        }
        nanosleep(&pollInterval, NULL);
    }

    if(discoveryStarted)
    {
        sd_bus_call_method(bus, BLUEZ_SERVICE, adapterPath, BLUEZ_ADAPTER_INTERFACE, "StopDiscovery", &error, NULL, "");
        sd_bus_error_free(&error);
    }
    if(busResult == 0)
    {
        return -EHOSTUNREACH;
    }
    return busResult > 0 ? 0 : busResult;
}

static int ReceivePairReply(sd_bus_message *reply, void *userData, sd_bus_error *error)
{
    PairResult *pairResult = (PairResult *)userData;
    const sd_bus_error *replyError = sd_bus_message_get_error(reply);

    (void)error;
    pairResult->finished = 1;
    if(replyError == NULL || sd_bus_error_has_name(replyError, "org.bluez.Error.AlreadyExists"))
    {
        pairResult->error = 0;
    }
    else if(sd_bus_error_has_names(replyError, "org.bluez.Error.AuthenticationFailed", "org.bluez.Error.AuthenticationRejected", "org.bluez.Error.AuthenticationCanceled"))
    {
        pairResult->error = EACCES;
    }
    else if(sd_bus_error_has_name(replyError, "org.freedesktop.DBus.Error.NoReply") || sd_bus_error_has_name(replyError, "org.bluez.Error.AuthenticationTimeout"))
    {
        pairResult->error = ETIMEDOUT;
    }
    else
    {
        pairResult->error = EIO;
    }
    return 0;
}

// 페어링 후 Trusted로 표시. 이미 페어링돼 있으면 Trusted만 설정.
static int PairDevice(sd_bus *bus, const char *devicePath, int timeoutSeconds)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *request = NULL;
    sd_bus_slot *pairSlot = NULL;
    PairResult pairResult = {0, 0};
    uint64_t deadline;
    int paired = 0;
    int busResult;

    busResult = sd_bus_get_property_trivial(bus, BLUEZ_SERVICE, devicePath, BLUEZ_DEVICE_INTERFACE, "Paired", &error, 'b', &paired);
    sd_bus_error_free(&error);
    if(busResult < 0)
    {
        return busResult;
    }

    if(!paired)
    {
        busResult = sd_bus_message_new_method_call(bus, &request, BLUEZ_SERVICE, devicePath, BLUEZ_DEVICE_INTERFACE, "Pair");
        if(busResult < 0)
        {
            return busResult;
        }
        busResult = sd_bus_call_async(bus, &pairSlot, request, ReceivePairReply, &pairResult, (uint64_t)timeoutSeconds * 1000000U);
        sd_bus_message_unref(request);
        if(busResult < 0)
        {
            return busResult;
        }

        // Pair 응답을 기다리는 동안 에이전트의 RequestPinCode도 여기서 처리됨
        deadline = GetMonotonicMs() + (uint64_t)timeoutSeconds * 1000U;
        while(!pairResult.finished)
        {
            uint64_t now;

            busResult = sd_bus_process(bus, NULL);
            if(busResult < 0)
            {
                break;
            }
            if(busResult > 0)
            {
                continue;
            }
            now = GetMonotonicMs();
            if(now >= deadline)
            {
                busResult = -ETIMEDOUT;
                break;
            }
            busResult = sd_bus_wait(bus, (deadline - now) * 1000U);
            if(busResult < 0 && busResult != -EINTR)
            {
                break;
            }
        }
        sd_bus_slot_unref(pairSlot);
        if(!pairResult.finished)
        {
            return busResult < 0 ? busResult : -ETIMEDOUT;
        }
        if(pairResult.error != 0)
        {
            return -pairResult.error;
        }
    }

    busResult = sd_bus_set_property(bus, BLUEZ_SERVICE, devicePath, BLUEZ_DEVICE_INTERFACE, "Trusted", &error, "b", 1);
    sd_bus_error_free(&error);
    return busResult < 0 ? busResult : 0;
}

// 검색을 켜고 scanSeconds 동안 기다린 뒤 BlueZ 객체 목록에서 장치를 모음. 0 또는 -errno
static int ScanWithDiscovery(sd_bus *bus, ScanList *scanList, int scanSeconds)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char devicePath[BLUEZ_PATH_SIZE];
    char adapterPath[BLUEZ_PATH_SIZE];
    struct timespec scanTime = {scanSeconds, 0};
    int discoveryStarted = 0;
    int busResult;

    // 빈 MAC은 어떤 장치와도 맞지 않으므로 어댑터 경로만 얻음
    busResult = FindDevicePath(bus, "", devicePath, adapterPath);
    if(busResult < 0)
    {
        return busResult;
    }
    if(adapterPath[0] == '\0')
    {
        return -ENODEV;
    }

    // HC-05는 클래식 BT라 BLE 장치는 제외. 필터는 이 버스 연결의 검색에만 적용되고 실패해도 검색은 진행
    sd_bus_call_method(bus, BLUEZ_SERVICE, adapterPath, BLUEZ_ADAPTER_INTERFACE, "SetDiscoveryFilter", &error, NULL, "a{sv}", 1, "Transport", "s", "bredr");
    sd_bus_error_free(&error);

    busResult = sd_bus_call_method(bus, BLUEZ_SERVICE, adapterPath, BLUEZ_ADAPTER_INTERFACE, "StartDiscovery", &error, NULL, "");
    if(busResult >= 0)
    {
        discoveryStarted = 1;
    }
    else if(!sd_bus_error_has_name(&error, "org.bluez.Error.InProgress"))
    {
        sd_bus_error_free(&error);
        return busResult;
    }
    sd_bus_error_free(&error);

    while(nanosleep(&scanTime, &scanTime) != 0 && errno == EINTR)
    {
    }

    busResult = sd_bus_call_method(bus, BLUEZ_SERVICE, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", &error, &reply, "");
    if(busResult >= 0)
    {
        busResult = ReadScanObjects(reply, scanList);
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if(discoveryStarted)
    {
        sd_bus_call_method(bus, BLUEZ_SERVICE, adapterPath, BLUEZ_ADAPTER_INTERFACE, "StopDiscovery", &error, NULL, "");
        sd_bus_error_free(&error);
    }
    return busResult < 0 ? busResult : 0;
}

// GetManagedObjects 응답(a{oa{sa{sv}}})의 모든 객체를 훑음. 0 이상 또는 -errno
static int ReadScanObjects(sd_bus_message *reply, ScanList *scanList)
{
    int busResult = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");

    while(busResult >= 0 && (busResult = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0)
    {
        busResult = sd_bus_message_skip(reply, "o");
        if(busResult >= 0)
        {
            busResult = ReadScanInterfaces(reply, scanList);
        }
        if(busResult >= 0)
        {
            busResult = sd_bus_message_exit_container(reply);
        }
    }
    if(busResult >= 0)
    {
        busResult = sd_bus_message_exit_container(reply);
    }
    return busResult;
}

// 객체 하나의 인터페이스 목록(a{sa{sv}})에서 Device1만 읽음. 0 이상 또는 -errno
static int ReadScanInterfaces(sd_bus_message *reply, ScanList *scanList)
{
    int busResult = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");

    while(busResult >= 0 && (busResult = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0)
    {
        const char *interfaceName;

        busResult = sd_bus_message_read(reply, "s", &interfaceName);
        if(busResult >= 0 && strcmp(interfaceName, BLUEZ_DEVICE_INTERFACE) == 0)
        {
            busResult = ReadScanDevice(reply, scanList);
        }
        else if(busResult >= 0)
        {
            busResult = sd_bus_message_skip(reply, "a{sv}");
        }
        if(busResult >= 0)
        {
            busResult = sd_bus_message_exit_container(reply);
        }
    }
    if(busResult >= 0)
    {
        busResult = sd_bus_message_exit_container(reply);
    }
    return busResult;
}

// Device1 속성(a{sv})을 읽어 이번 검색에서 보였고(RSSI 있음) 이름 필터에 맞으면 목록에 추가
// 예전에 페어링만 해 두고 지금 근처에 없는 장치는 RSSI가 없어서 빠짐
static int ReadScanDevice(sd_bus_message *reply, ScanList *scanList)
{
    BluetoothScanDevice device;
    int hasAddress = 0;
    int hasRssi = 0;
    int busResult;

    memset(&device, 0, sizeof(device));
    busResult = sd_bus_message_enter_container(reply, 'a', "{sv}");
    while(busResult >= 0 && (busResult = sd_bus_message_enter_container(reply, 'e', "sv")) > 0)
    {
        const char *propertyName;
        const char *text;

        busResult = sd_bus_message_read(reply, "s", &propertyName);
        if(busResult >= 0 && strcmp(propertyName, "Address") == 0)
        {
            busResult = sd_bus_message_read(reply, "v", "s", &text);
            if(busResult >= 0 && strlen(text) == BLUETOOTH_MAC_SIZE)
            {
                memcpy(device.mac, text, BLUETOOTH_MAC_TEXT_SIZE);
                hasAddress = 1;
            }
        }
        else if(busResult >= 0 && strcmp(propertyName, "Name") == 0)
        {
            busResult = sd_bus_message_read(reply, "v", "s", &text);
            if(busResult >= 0)
            {
                snprintf(device.name, sizeof(device.name), "%s", text);
            }
        }
        else if(busResult >= 0 && strcmp(propertyName, "RSSI") == 0)
        {
            busResult = sd_bus_message_read(reply, "v", "n", &device.rssi);
            hasRssi = busResult >= 0;
        }
        else if(busResult >= 0 && strcmp(propertyName, "Paired") == 0)
        {
            busResult = sd_bus_message_read(reply, "v", "b", &device.paired);
        }
        else if(busResult >= 0)
        {
            busResult = sd_bus_message_skip(reply, "v");
        }
        if(busResult >= 0)
        {
            busResult = sd_bus_message_exit_container(reply);
        }
    }
    if(busResult >= 0)
    {
        busResult = sd_bus_message_exit_container(reply);
    }
    if(busResult >= 0 && hasAddress && hasRssi && scanList->count < scanList->maxDevices && MatchesNameFilter(device.name, scanList->nameFilter))
    {
        scanList->devices[scanList->count++] = device;
    }
    return busResult;
}

// 대소문자 무시 부분 일치. 필터가 없으면 항상 1
static int MatchesNameFilter(const char *name, const char *nameFilter)
{
    size_t filterLength;

    if(nameFilter == NULL || nameFilter[0] == '\0')
    {
        return 1;
    }
    filterLength = strlen(nameFilter);
    for(const char *start = name; *start != '\0'; ++start)
    {
        if(strncasecmp(start, nameFilter, filterLength) == 0)
        {
            return 1;
        }
    }
    return 0;
}

void DisconnectBluetoothDevice(int bluetoothFd)
{
    if(bluetoothFd >= 0)
    {
        shutdown(bluetoothFd, SHUT_RDWR);
        close(bluetoothFd);
    }
}
