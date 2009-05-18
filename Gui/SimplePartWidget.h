#ifndef SIMPLEPARTWIDGET_H
#define SIMPLEPARTWIDGET_H

#include "EmbeddedWebView.h"
#include "Imap/Network/MsgPartNetAccessManager.h"

namespace Gui {

/** @short Widget that handles display of primitive message parts

More complicated parts are handled by other widgets. Role of this one is to
simply render data that can't be broken down to more trivial pieces.
*/

class SimplePartWidget : public EmbeddedWebView
{
    Q_OBJECT
public:
    SimplePartWidget( QWidget* parent,
                      Imap::Network::MsgPartNetAccessManager* manager,
                      Imap::Mailbox::TreeItemPart* part );
};

}

#endif // SIMPLEPARTWIDGET_H
