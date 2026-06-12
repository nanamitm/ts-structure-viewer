#pragma once

#include "TsScan.h"

#include <QObject>
#include <atomic>

// Runs TsScan::scanFile on its own thread so the UI stays responsive: it relays
// the scan's progress as a queued signal and delivers the result via finished().
// cancel() sets an atomic flag the scan's progress callback checks.
class TsScanWorker : public QObject {
    Q_OBJECT

public:
    void configure(const QString& path)
    {
        m_path = path;
        m_cancel.store(false);
    }

public slots:
    void run();
    void cancel() { m_cancel.store(true); }

signals:
    void progress(qint64 done, qint64 total);
    void finished(const TsScanResult& result);

private:
    QString m_path;
    std::atomic_bool m_cancel{ false };
};
