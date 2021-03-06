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

#include "eranvalintamodel.h"
#include <QSqlQuery>
#include "kirjanpito.h"

#include <QHash>
#include <QPair>

#include <QDebug>

EranValintaModel::EranValintaModel()
{

}

int EranValintaModel::rowCount(const QModelIndex & /* parent */) const
{
    return erat_.count() + 1;
}

QVariant EranValintaModel::data(const QModelIndex &index, int role) const
{
    if( index.row() == 0)
    {
        // Ensimmäinen rivi jolla ei ole erää
        if( role == EraIdRooli)
            return QVariant(0);
        else if(  role == Qt::DisplayRole )
            return QVariant(tr("Muodosta uusi tase-erä"));
    }
    else if( index.isValid() )
    {
        TaseEra era = erat_.value(index.row()-1);

        if( role == EraIdRooli || role == Qt::UserRole)
            return QVariant( era.eraId);
        else if( role == PvmRooli)
            return QVariant( era.pvm);
        else if( role == SeliteRooli)
            return QVariant( era.selite);
        else if( role == SaldoRooli)
            return QVariant( era.saldoSnt);
        else if( role == TositteenTunnisteRooli)
            return QVariant( era.tositteenTunniste() );
        else if(role == Qt::DisplayRole)
        {
            return QVariant( QString("%1 %2 (%L3 €)").arg(era.pvm.toString("dd.MM.yyyy")).arg(era.selite).arg(era.saldoSnt / 100.0,0,'f',2));
        }
    }
    return QVariant();
}



void EranValintaModel::lataa(Tili tili, bool kaikki, QDate paivalle)
{
    beginResetModel();
    erat_.clear();

    QSqlQuery query( *(kp()->tietokanta()) ) ;

    // avaimena eraid, arvona saldo (debet - kredit)
    QHash<int, qlonglong > saldot;

    QString pvmehto;
    if( paivalle.isValid())
        pvmehto = QString("and pvm <= \"%1\" ").arg(paivalle.toString(Qt::ISODate));

    query.exec(QString("SELECT eraid, sum(debetsnt) as debetit, sum(kreditsnt) as kreditit from vienti "
                       "where tili=%1 and eraid is not null %2 group by eraid").arg(tili.id()).arg(pvmehto));

    // Tallennetaan saldotaulukkoon tilien eräsaldot
    while( query.next() )
    {
        saldot.insert( query.value("eraid").toInt(), query.value("debetit").toLongLong() - query.value("kreditit").toLongLong() );
    }

    query.exec(QString("SELECT id, pvm, selite, debetsnt, kreditsnt from vienti "
               "where tili=%1 and eraid is NULL %2 order by pvm").arg(tili.id()).arg(pvmehto));

    while( query.next())
    {
        int id = query.value("id").toInt();
        quint64 saldo = saldot.value(id, 0) + query.value("debetsnt").toLongLong() - query.value("kreditsnt").toLongLong();
        if( saldo || kaikki )
        {
            // Tämä tase-erä ei ole mennyt tasan, joten se on valittavissa
            TaseEra era;
            era.eraId = id;
            era.pvm = query.value("pvm").toDate();
            era.selite = query.value("selite").toString();
            era.saldoSnt = saldo;
            erat_.append(era);
        }

    }
    endResetModel();
}

TaseEra::TaseEra(int id)
{
    eraId = id;

    // Jos id annetaan rakentajaan, hakee halutun erän tiedot
    if(id)
    {
        QSqlQuery query( *( kp()->tietokanta() ));
        query.exec(QString("SELECT sum(debetsnt),sum(kreditsnt) as saldo from vienti "
                           "where eraid=%1").arg(id ));
        if( query.next() )
        {
            saldoSnt = query.value(0).toLongLong();
            saldoSnt -= query.value(1).toLongLong();
        }

        query.exec(QString("SELECT pvm, selite, debetsnt, kreditsnt from vienti "
                   "where id=%1").arg( id ));
        if( query.next())
        {
            pvm = query.value("pvm").toDate();
            selite = query.value("selite").toLongLong();
            saldoSnt += query.value("debetsnt").toLongLong();
            saldoSnt -= query.value("kreditsnt").toLongLong();
        }

    }
}

QString TaseEra::tositteenTunniste()
{
    if(eraId)
    {
        QSqlQuery query(QString("select tositelaji,tunniste from vientivw where vientiId=%1").arg(eraId));
        if( query.next())
        {
            return  query.value("tositelaji").toString() + query.value("tunniste").toString();
        }
    }
    return QString();
}
