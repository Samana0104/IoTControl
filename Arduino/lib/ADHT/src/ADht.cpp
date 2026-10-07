#include "ADht.h"


void ADht::Begin()
{
    dht.begin();
}

float ADht::GetTemperature()
{
    return dht.readTemperature();
}

float ADht::GetHumidity()
{
    return dht.readHumidity();
}