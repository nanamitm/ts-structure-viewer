#include "TsScanWorker.h"

void TsScanWorker::run()
{
    const TsScanResult r = TsScan::scanFile(m_path, [this](qint64 done, qint64 total) {
        emit progress(done, total);
        return !m_cancel.load();
    });
    emit finished(r);
}
