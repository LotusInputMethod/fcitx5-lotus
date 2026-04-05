#ifndef OSK_CONTROLLER_H
#define OSK_CONTROLLER_H

#include <QObject>
#include <QVariant>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>

class OSKWindow;

class OSKController : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "app.lotus.Osk.Controller1")
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)

  public:
    explicit OSKController(QObject* parent = nullptr);
    virtual ~OSKController();

    bool visible() const {
        return m_visible;
    }
    void setVisible(bool visible);

    void showWindow();
    void hideWindow();

    // Key submission
    Q_INVOKABLE void sendKey(uint keyval, bool isRelease = false, uint keycode = 0, bool shift = false);
    Q_INVOKABLE bool queryCapsLockState();

  public slots:
    // DBus methods matching the service expectations
    void Show();
    void Hide();
    void Toggle();

  signals:
    void visibleChanged();

  private:
    void       connectToServer();
    void       notifyServerVisibility();
    bool       m_visible  = false;
    OSKWindow* m_window   = nullptr;
    int        m_socketFd = -1;
};

#endif // OSK_CONTROLLER_H
