/* Copyright (C) 2007 Jan Kundrát <jkt@gentoo.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "ParserMonitor.h"

namespace Demo {

ParserMonitor::ParserMonitor( QObject* parent, Imap::Parser* parser ):
    QObject(parent), _parser(parser)
{
    _stream = new QTextStream( stderr );
    connect( _parser, SIGNAL( responseReceived() ), this, SLOT( responseReceived() ) );
}

void ParserMonitor::responseReceived()
{
    while ( _parser->hasResponse() ) {
        std::tr1::shared_ptr<Imap::Responses::AbstractResponse> resp = _parser->getResponse();
        if ( resp )
            *_stream << *resp << "\r\n" << flush;
        else
            *_stream << "(null)\r\n" << flush;
    }
}

}

#include "ParserMonitor.moc"
