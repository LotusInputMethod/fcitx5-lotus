#ifndef OSK_WINDOW_H
#define OSK_WINDOW_H

#include <QWidget>
#include <QMap>
#include <QPushButton>
#include <QTemporaryFile>
#include "osk_theme.h"

class OSKController;

class OSKWindow : public QWidget {
    Q_OBJECT

  public:
    explicit OSKWindow(OSKController* controller, QWidget* parent = nullptr);
    ~OSKWindow();

    void setWhiteTheme(bool white);
    void setOSKSize(const QString& size);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    struct KeyData {
        uint keycode;
        uint keysym;
        uint keysymUpper;
    };
    void                            setupLayout(const Lotus::OSKTheme& theme);
    void                            updateKeyLabels();
    QPair<uint, uint>               getKeyInfo(const QString& key) const;
    QString                         getButtonStyle(const QString& bg, const QString& fg, const QString& extra = "") const;

    OSKController*                  m_controller;
    Lotus::OSKTheme                 m_theme;
    bool                            m_capsLockActive = false;
    bool                            m_shiftActive    = false;
    bool                            m_whiteTheme     = false;
    int                             m_baseWidth      = 1100;
    int                             m_baseHeight     = 380;
    int                             m_fontSize       = 20;
    double                          m_scaleFactor    = 1.0;
    QMap<QString, QPushButton*>     m_alphabetButtons;
    QMap<QString, QPushButton*>     m_symbolButtons;
    QList<QPushButton*>             m_specialButtons;
    QMap<QPushButton*, QString>     m_buttonExtraStyles;

    int                             m_kwinScriptId = -1;
    mutable QHash<QString, QString> m_styleCache;
};

#endif // OSK_WINDOW_H
