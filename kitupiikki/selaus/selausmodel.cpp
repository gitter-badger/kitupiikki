/*
   Copyright (C) 2017 Arto Hyvättinen

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "selausmodel.h"

#include <QSqlQuery>
#include "db/kirjanpito.h"

#include <QDebug>

SelausModel::SelausModel(Kirjanpito *kp) :
    kirjanpito(kp)
{

}

int SelausModel::rowCount(const QModelIndex & /* parent */) const
{
    return rivit.count();
}

int SelausModel::columnCount(const QModelIndex & /* parent */) const
{
    return 5;
}

QVariant SelausModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter | Qt::AlignVCenter);
    else if( role != Qt::DisplayRole)
        return QVariant();
    else if( orientation == Qt::Horizontal )
    {
        switch (section)
        {
        case PVM :
            return QVariant("Pvm");
        case TILI:
            return QVariant("Tili");
        case DEBET :
            return QVariant("Debet");
        case KREDIT:
            return QVariant("Kredit");
        case SELITE:
            return QVariant("Selite");
        }
    }

    return QVariant( section + 1  );
}

QVariant SelausModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();
    if( role == Qt::DisplayRole || role == Qt::EditRole)
    {
        SelausRivi rivi = rivit[ index.row()];
        switch (index.column())
        {
            case PVM: return QVariant( rivi.pvm );

            case TILI:
                if( rivi.tili.numero())
                    return QVariant( QString("%1 %2").arg(rivi.tili.numero()).arg(rivi.tili.nimi()) );
                else
                    return QVariant();

            case DEBET:
                if( role == Qt::EditRole)
                    return QVariant( rivi.debetSnt);
                else if( rivi.debetSnt )
                    return QVariant( QString("%L1 €").arg(rivi.debetSnt / 100.0,0,'f',2));
                else
                    return QVariant();

            case KREDIT:
                if( role == Qt::EditRole)
                    return QVariant( rivi.kreditSnt);
                else if( rivi.kreditSnt )
                    return QVariant( QString("%L1 €").arg(rivi.kreditSnt / 100.0,0,'f',2));
                else
                    return QVariant();

            case SELITE: return QVariant( rivi.selite );

        }
    }
    else if( role == Qt::TextAlignmentRole)
    {
        if( index.column()==KREDIT || index.column() == DEBET)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        else
            return QVariant( Qt::AlignLeft | Qt::AlignVCenter);

    }
    return QVariant();
}

void SelausModel::lataa(const QDate &alkaa, const QDate &loppuu)
{
    QString kysymys = QString("SELECT tosite, pvm, tili, debetsnt, kreditsnt, selite "
                              "FROM vienti WHERE pvm BETWEEN \"%1\" AND \"%2\" "
                              "ORDER BY pvm, id")
                              .arg( alkaa.toString(Qt::ISODate ) )
                              .arg( loppuu.toString(Qt::ISODate)) ;

    beginResetModel();
    rivit.clear();
    tileilla.clear();

    qDebug() << kysymys;

    QSqlQuery query;
    query.exec(kysymys);
    while( query.next())
    {
        SelausRivi rivi;
        rivi.tositeId = query.value(0).toInt();
        rivi.pvm = query.value(1).toDate();
        rivi.tili = kirjanpito->tili( query.value(2).toInt());
        rivi.debetSnt = query.value(3).toInt();
        rivi.kreditSnt = query.value(4).toInt();
        rivi.selite = query.value(5).toString();
        rivit.append(rivi);

        QString tilistr = QString("%1 %2")
                    .arg(rivi.tili.numero())
                    .arg(rivi.tili.nimi());
        if( !tileilla.contains(tilistr))
            tileilla.append(tilistr);
    }

    tileilla.sort();
    endResetModel();
}