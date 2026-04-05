#ifndef OSK_CONTROLLER_H
#define OSK_CONTROLLER_H

#include <QObject>
#include <QVariant>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QTimer>
#include <QSocketNotifier>

class OSKWindow;

class OSKController : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "app.lotus.Osk.Controller1")
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool capsLockActive READ capsLockActive NOTIFY capsLockActiveChanged)

  public:
    explicit OSKController(QObject* parent = nullptr);
    virtual ~OSKController();

    bool visible() const {
        return m_visible;
    }
    bool capsLockActive() const {
        return m_capsLockActive;
    }
    void setVisible(bool visible);

    void showWindow();
    void hideWindow();

    // Key submission
    Q_INVOKABLE void sendKey(uint keyval, bool isRelease = false, uint keycode = 0, bool shift = false);
    Q_INVOKABLE void queryCapsLockState(); // Now async

  public slots:
    // DBus methods matching the service expectations
    void Show();
    void Hide();
    void Toggle();

  signals:
    void visibleChanged();
    void capsLockActiveChanged();

  private slots:
    void handleSocketActivated(int socket);

  private:
    void             connectToServer();
    void             notifyServerVisibility();
    bool             m_visible        = false;
    bool             m_capsLockActive = false;
    OSKWindow*       m_window         = nullptr;
    int              m_socketFd       = -1;
    QSocketNotifier* m_notifier       = nullptr;
    QTimer           m_hideTimer;
};

#endif // OSK_CONTROLLER_H
