#pragma once
#include <QDialog>
class QLineEdit; class QComboBox;

class ConnectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectDialog (QWidget* parent = nullptr);
    QString user () const;
    QString password () const;
    QString alias () const;
    QString tnsAdmin () const;
private slots:
    void browse ();
    void reloadAliases ();
private:
    QLineEdit* m_user; QLineEdit* m_pass; QComboBox* m_alias; QLineEdit* m_tns;
};
