#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

typedef struct _DhtRecord
{
    QString id;
    double temp = 0;
    double humi = 0;
    QDateTime updatedAt;
    QString memberType;
} DhtRecord;

typedef QList<DhtRecord> DhtRecords;

Q_DECLARE_METATYPE(DhtRecords)
