#ifndef OSK_WINDOW_H
#define OSK_WINDOW_H

#include <QWidget>
#include <QMap>
#include <QPushButton>

class OSKController;

class OSKWindow : public QWidget {
    Q_OBJECT

  public:
    explicit OSKWindow(OSKController* controller, QWidget* parent = nullptr);
    ~OSKWindow();

  protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void                        setupLayout();
    void                        updateKeyLabels();
    OSKController*              m_controller;
    bool                        m_capsLockActive = false;
    QMap<QString, QPushButton*> m_alphabetButtons;
    QList<QPushButton*>         m_specialButtons;
    QMap<QPushButton*, QString> m_buttonExtraStyles;
};

#endif // OSK_WINDOW_H
