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

#include "tilinavausmodel.h"

#include <QSqlQuery>
#include <QMessageBox>
#include <QSqlError>
#include <QDebug>

TilinavausModel::TilinavausModel() :
    muokattu_(false)
{

}

int TilinavausModel::rowCount(const QModelIndex & /* parent */ ) const
{
    return kp()->tilit()->rowCount( QModelIndex());
}

int TilinavausModel::columnCount(const QModelIndex & /* parent */) const
{
    return 3;
}

QVariant TilinavausModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter | Qt::AlignVCenter);
    else if( orientation == Qt::Horizontal && role == Qt::DisplayRole )
    {
        switch (section)
        {
        case NRO :
            return QVariant("Numero");
        case NIMI:
            return QVariant("Tili");
        case SALDO :
            return QVariant("Alkusaldo");
        }
    }
    return QVariant();
}

QVariant TilinavausModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();
    if( role == Qt::DisplayRole || role == Qt::EditRole)
    {
        Tili tili = kp()->tilit()->tiliIndeksilla( index.row());

        switch (index.column())
        {
            case NRO: return QVariant( tili.numero() );

            case NIMI:
                    return QVariant( tili.nimi());

            case SALDO:
                if( tili.onko(TiliLaji::KAUDENTULOS))
                    return QVariant("*****");   // Kauden tulos lasketaan


                int saldo = saldot.value( tili.numero(), 0);
                if( role == Qt::EditRole)
                    return QVariant(saldo);

                double saldod = (double) saldo / 100.0;
                if( saldod)
                    return QVariant( QString("%L1 €").arg( saldod, 10,'f',2));
                else
                    return QVariant();

        }
    }
    else if( role == Qt::TextAlignmentRole)
    {
        if( index.column()==SALDO)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        else
            return QVariant( Qt::AlignLeft | Qt::AlignVCenter);

    }
    else if( role == Qt::FontRole)
    {
        Tili tili = kp()->tilit()->tiliIndeksilla( index.row());
        QFont fontti;
        if( tili.otsikkotaso() )
            fontti.setBold(true);
        return QVariant( fontti );
    }


    return QVariant();
}

Qt::ItemFlags TilinavausModel::flags(const QModelIndex &index) const
{
    Tili tili = kp()->tilit()->tiliIndeksilla( index.row());
    if( index.column() == SALDO && !tili.otsikkotaso() && !tili.onko(TiliLaji::KAUDENTULOS))
        return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
    else
        return QAbstractTableModel::flags(index);
}

bool TilinavausModel::setData(const QModelIndex &index, const QVariant &value, int /* role */)
{
    int tilinro = kp()->tilit()->tiliIndeksilla( index.row()).numero() ;

    if( value.toInt())
        saldot[tilinro] = value.toInt(); // Delegaatti käsittelee senttejä
    else
        saldot.remove(tilinro);          // Ei jätetä nollia kirjauksiin

    paivitaInfo();
    muokattu_ = true;
    return true;
}


void TilinavausModel::lataa()
{
    beginResetModel();
    saldot.clear();


    QSqlQuery kysely("select tili, debetsnt, kreditsnt from vienti where tosite=0");


    while(kysely.next())
    {
        Tili tili = Kirjanpito::db()->tilit()->tiliIdlla( kysely.value("tili").toInt() );
        int debet = kysely.value("debetsnt").toInt();
        int kredit = kysely.value("kreditsnt").toInt();

        if( tili.onko(TiliLaji::VASTAAVAA)  || tili.onko(TiliLaji::MENO))
            saldot[ tili.numero()] = saldot.value(tili.numero()) + debet - kredit;
        else
            saldot[ tili.numero()] = saldot.value(tili.numero()) + kredit - debet;

    }
    paivitaInfo();
    endResetModel();
    muokattu_ = false;
}

bool TilinavausModel::tallenna()
{

    QSqlQuery kysely("delete from vienti where tosite=0");

    QDate avauspaiva = Kirjanpito::db()->asetukset()->pvm("TilinavausPvm");


    kysely.prepare("INSERT INTO vienti(tosite,pvm,tili,debetsnt,kreditsnt,selite, vientirivi) "
                   "VALUES (0,:pvm,:tili,:debet,:kredit,\"Tilinavaus\", :vientirivi)");

    QMapIterator<int,int> iter(saldot);
    int vientirivi = 1;

    while( iter.hasNext())
    {
        iter.next();
        Tili tili = Kirjanpito::db()->tilit()->tiliNumerolla(iter.key() );
        kysely.bindValue(":pvm", avauspaiva);
        kysely.bindValue(":tili", tili.id());
        kysely.bindValue(":vientirivi", vientirivi);

        if( tili.onko(TiliLaji::VASTAAVAA) || tili.onko(TiliLaji::MENO) )
        {
            kysely.bindValue(":debet", iter.value());
            kysely.bindValue(":kredit", QVariant());
        }
        else
        {
            kysely.bindValue(":debet",QVariant());
            kysely.bindValue(":kredit", iter.value());
        }
        kysely.exec();
        qDebug() << kysely.lastQuery() << " " << kysely.lastError().text();
    }
    kp()->asetukset()->aseta("Tilinavaus",1);   // Tilit merkitään avatuiksi

    muokattu_ = false;
    return true;
}

void TilinavausModel::paivitaInfo()
{
    int tasevastaavaa = 0;
    int tasevastattavaa = 0;
    int tulos = 0;

    QMapIterator<int,int> iter(saldot);
    while( iter.hasNext() )
    {
        iter.next();
        Tili tili = Kirjanpito::db()->tilit()->tiliNumerolla( iter.key());
        if( tili.onko(TiliLaji::VASTAAVAA)  )
            tasevastaavaa += iter.value();
        else if( tili.onko(TiliLaji::VASTATTAVAA) )
            tasevastattavaa += iter.value();
        else if( tili.onko(TiliLaji::TULO) )
            tulos += iter.value();
        else if( tili.onko(TiliLaji::MENO) )
            tulos -= iter.value();
    }

    tasevastattavaa += tulos;

    QString txt = tr("Yli/alijäämä %L1 €<br>Vastaavaa %L2 €<br>Vastattavaa %L3 €")
            .arg( (double) tulos / 100, 0, 'f', 2 )
            .arg( (double) tasevastaavaa / 100, 0, 'f', 2 )
            .arg( (double) tasevastattavaa / 100, 0, 'f', 2 );
    if( tasevastaavaa != tasevastattavaa)
        txt += tr("<br><font color=red>Poikkeama %L1 € </font>").arg((double)(tasevastaavaa - tasevastattavaa) / 100, 0, 'f', 2  );

    emit infoteksti(txt);
}


