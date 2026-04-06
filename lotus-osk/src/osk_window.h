#ifndef OSK_WINDOW_H
#define OSK_WINDOW_H

#include <QWidget>
#include <QMap>
#include <QPushButton>
#include <QTemporaryFile>

class OSKController;

class OSKWindow : public QWidget {
    Q_OBJECT

  public:
    explicit OSKWindow(OSKController* controller, QWidget* parent = nullptr);
    ~OSKWindow();

    void setWhiteTheme(bool white);

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
    static constexpr const char* COLOR_BG_ACTIVE  = "#005a9e";
    static constexpr const char* COLOR_BG_NORMAL  = "#333333";
    static constexpr const char* COLOR_BG_SPECIAL = "#252525";
    static constexpr const char* COLOR_FG_NORMAL  = "#ffffff";
    static constexpr const char* COLOR_FG_SPECIAL = "#999999";
    static constexpr const char* COLOR_BORDER     = "#444444";
    static constexpr const char* COLOR_HOVER      = "#444444";
    static constexpr const char* COLOR_PRESSED    = "#555555";
    static constexpr const char* COLOR_WINDOW_BG  = "#191919";

    // Light Theme Colors
    static constexpr const char*    L_COLOR_BG_ACTIVE  = "#0078d4";
    static constexpr const char*    L_COLOR_BG_NORMAL  = "#ffffff";
    static constexpr const char*    L_COLOR_BG_SPECIAL = "#e5e5e5";
    static constexpr const char*    L_COLOR_FG_NORMAL  = "#000000";
    static constexpr const char*    L_COLOR_FG_SPECIAL = "#666666";
    static constexpr const char*    L_COLOR_BORDER     = "#cccccc";
    static constexpr const char*    L_COLOR_HOVER      = "#e1e1e1";
    static constexpr const char*    L_COLOR_PRESSED    = "#d1d1d1";
    static constexpr const char*    L_COLOR_WINDOW_BG  = "#f0f0f0";

    void                            setupLayout(int keyWidth, int keyHeight, int spacing, int margin);
    void                            updateKeyLabels();
    QPair<uint, uint>               getKeyInfo(const QString& key) const;
    QString                         getButtonStyle(const QString& bg = "#333333", const QString& fg = "#ffffff", const QString& extra = "") const;

    OSKController*                  m_controller;
    bool                            m_capsLockActive = false;
    bool                            m_shiftActive    = false;
    bool                            m_whiteTheme     = false;
    int                             m_baseWidth      = 1100;
    int                             m_baseHeight     = 380;
    int                             m_fontSize       = 20;
    QMap<QString, QPushButton*>     m_alphabetButtons;
    QMap<QString, QPushButton*>     m_symbolButtons;
    QList<QPushButton*>             m_specialButtons;
    QMap<QPushButton*, QString>     m_buttonExtraStyles;

    int                             m_kwinScriptId = -1;
    mutable QHash<QString, QString> m_styleCache;
};

#endif // OSK_WINDOW_H
