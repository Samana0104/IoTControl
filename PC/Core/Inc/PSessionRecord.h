#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtTypes>

typedef struct _SessionRecord
{
    QString id;
    QString memberType;
    quint8 links = 0;
} SessionRecord;

typedef QList<SessionRecord> SessionRecords;

Q_DECLARE_METATYPE(SessionRecords)
