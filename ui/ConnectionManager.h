#pragma once
// Gestor de conexiones: lista de conexiones guardadas (con filtros por columna) + formulario de edición.
#include "ConnectionStore.h"
#include <QDialog>
#include <QVariantMap>
class QCheckBox; class QComboBox; class QLabel; class QLineEdit; class QPushButton; class QStandardItemModel; class QTableView; class QSortFilterProxyModel;
class ColumnFilterProxy;

class ConnectionManager : public QDialog
{
    Q_OBJECT
public:
    ConnectionManager (ConnectionStore* store, QWidget* parent = nullptr);
    QVariantMap connectParams () const { return m_params; }      // al aceptar: todo lo necesario para conectar (incluye contraseña)
    QString profileId () const { return m_currentId; }
    QString tag () const { return m_params.value("tag").toString(); }
    void showTestResult (bool ok, const QString& message);       // lo llama MainWindow cuando termina "Probar"
    ConnectionProfile currentProfile () const;                   // lo que hay en el formulario (sin contraseña)
    void selectById (const QString& id);
signals:
    void testRequested (const QVariantMap& params);
private slots:
    void onRowChanged ();
    void newConnection ();
    void saveConnection ();
    void deleteConnection ();
    void connectNow ();
    void testNow ();
    void browseTns ();
    void reloadAliases ();
    void updateBypassState ();
    void openSettings ();
    QString globalTns () const;                        // carpeta TNS global (ajuste), con ~ expandido
    QString effectiveTns () const;                     // la del formulario o, si está vacía, la global
    void updateTnsInfo ();
    void importFromSqlTools ();
private:
    void reloadList (const QString& selectId = QString());
    void fillForm (const ConnectionProfile& p);
    bool validate (QString* why) const;
    bool resolvePassword (QString* pw);                           // del formulario o, si está vacío, del Llavero
    QVariantMap paramsFor (const ConnectionProfile& p, const QString& password) const;

    ConnectionStore* m_store;
    QStandardItemModel* m_model; ColumnFilterProxy* m_proxy; QTableView* m_table; QList<QLineEdit*> m_filters;
    QLineEdit *m_tag, *m_user, *m_pass, *m_tns, *m_host, *m_port, *m_sid;
    QComboBox *m_alias, *m_mode;
    QCheckBox *m_bypass, *m_service, *m_readOnly, *m_slow, *m_savePw;
    QLabel* m_result; QLabel* m_tnsInfo; QWidget* m_bypassBox = nullptr;
    QPushButton *m_btnConnect, *m_btnSave, *m_btnTest, *m_btnDelete;
    QString m_currentId; int m_count = 0; QDateTime m_last;
    QVariantMap m_params;
};
