/* Copyright (C) 2007 - 2012 Jan Kundrát <jkt@flaska.net>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or version 3 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/


#include "GenUrlAuthTask.h"
#include <QUrl>
#include "GetAnyConnectionTask.h"
#include "ItemRoles.h"
#include "Model.h"

namespace Imap
{
namespace Mailbox
{

GenUrlAuthTask::GenUrlAuthTask(Model *model, const QString &host, const QString &user, const QString &mailbox,
                               const uint uidValidity, const uint uid, const QString &part, const QString &access):
    ImapTask(model)
{
    req = QString::fromAscii("imap://%1@%2/%3;UIDVALIDITY=%4/;UID=%5%6;urlauth=%7")
            .arg(user, host, QUrl::toPercentEncoding(mailbox), QString::number(uidValidity), QString::number(uid),
                 part.isEmpty() ? QString(): QString::fromAscii("/;section=%1").arg(part),
                 access );
    conn = model->m_taskFactory->createGetAnyConnectionTask(model);
    conn->addDependentTask(this);
}

void GenUrlAuthTask::perform()
{
    parser = conn->parser;
    Q_ASSERT(parser);
    markAsActiveTask();

    IMAP_TASK_CHECK_ABORT_DIE;

    tag = parser->genUrlAuth(req.toAscii(), "INTERNAL");
}

bool GenUrlAuthTask::handleStateHelper(const Imap::Responses::State *const resp)
{
    if (resp->tag.isEmpty())
        return false;

    if (resp->tag == tag) {

        if (resp->kind == Responses::OK) {
            // nothing should be needed here
            _completed();
        } else {
            _failed(resp->message);
        }
        return true;
    } else {
        return false;
    }
}

bool GenUrlAuthTask::handleGenUrlAuth(const Responses::GenUrlAuth *const resp)
{
    emit gotAuth(resp->url);
    return true;
}

QVariant GenUrlAuthTask::taskData(const int role) const
{
    return role == RoleTaskCompactName ? QVariant(tr("Obtaining authentication token")) : QVariant();
}

}
}