#pragma once
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCache>
#include <QMutex>
#include <QFuture>


//GDAL does not like the slots macro for Qt
// #ifdef slots
// #undef slots
// #endif
#include "gdal_priv.h"
#include "gdalwarper.h"
#include "gdal_utils.h"

// Minimal valid 1x1 transparent PNG (68 bytes), hardcoded to avoid runtime cost
static const uint8_t kEmptyPng[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A, // PNG signature
    0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52, // IHDR chunk
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01, // 1x1
    0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4, // 8-bit RGBA
    0x89,0x00,0x00,0x00,0x0B,0x49,0x44,0x41, // IDAT chunk
    0x54,0x08,0xD7,0x63,0x60,0x00,0x00,0x00, // compressed: transparent pixel
    0x02,0x00,0x01,0xE2,0x21,0xBC,0x33,0x00,
    0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE, // IEND chunk
    0x42,0x60,0x82
};

class OrthomosaicBackend : public QObject
{
    Q_OBJECT
    //Orthomosaic display
    Q_PROPERTY(double orthoProgress READ orthoProgress NOTIFY orthoProgressChanged)
    Q_PROPERTY(double orthoOpacity READ orthoOpacity WRITE setOrthoOpacity NOTIFY orthoOpacityChanged)
    Q_PROPERTY(QString orthoStatus READ orthoStatus NOTIFY orthoStatusChanged)
    Q_PROPERTY(QString orthoTileUrl READ orthoTileUrl NOTIFY orthoReadyChanged)
    Q_PROPERTY(QString orthoFileName READ orthoFileName NOTIFY orthoFileNameChanged)
    Q_PROPERTY(bool orthoReady READ orthoReady NOTIFY orthoReadyChanged)
    Q_PROPERTY(bool orthoProcessing READ orthoProcessing NOTIFY orthoProcessingChanged)

public:
    explicit OrthomosaicBackend(QObject* parent = nullptr);

    double orthoProgress() const {return this->ortho_progress_; }
    double orthoOpacity() const {return this->ortho_opacity_; }
    QString orthoStatus() const {return this->ortho_status_; }
    QString orthoTileUrl() const {
        qWarning() << "Ortho Tile URL: " << this->ortho_tile_url_;
        return this->ortho_tile_url_; }
    QString orthoFileName() const {return this->ortho_file_name_; }
    bool orthoReady() const {return this->ortho_ready_; }
    bool orthoProcessing() const {return this->ortho_processing_; }
    void cancelOrtho() { this->ortho_cancelled_ = true; };
    
    Q_INVOKABLE void loadGeoTiff(const QString& path);
    // Q_INVOKABLE void cancel();
    Q_INVOKABLE void setOrthoOpacity(const double opacity) { ortho_opacity_ = opacity; }

signals:
    void orthoProgressChanged();
    void orthoOpacityChanged();
    void orthoStatusChanged();
    void orthoReadyChanged();
    void orthoProcessingChanged();
    void orthoFileNameChanged();
    void errorOccurred(const QString& message);

private:
    //Orthomosaic Steps
    bool stepValidateAndOpen(const QString& path); // ~0-5%
    bool stepReprojectToWebMercator(); // ~5-40%
    bool stepBuildOverviews(); // ~40-70%
    bool stepConvertToCOG(); // ~70-85%
    bool stepStartTileServer(); // ~85-100%
    void sendEmptyTile(QTcpSocket* socket);

    void handleTileRequest(QTcpSocket* socket, const QByteArray& request);
    QByteArray renderTile(int32_t z, int32_t x, int32_t y);
    QByteArray renderTileFromGDAL(int32_t z, int32_t x, int32_t y);
    void cleanupPreviousSession();
    void setOrthoProgress(double prog, const QString& status);
    GDALDataset* acquireDataset();
    void releaseDataset(GDALDataset* ds);
    std::atomic<bool> cancelled_{false};

        //ortho settings
    GDALDataset* gdal_dataset_ {nullptr};
    GDALDataset* warped_dataset_ {nullptr};
    QTcpServer* qtcp_server_ {nullptr};
    QHash<QTcpSocket*,QByteArray> socket_buffers_;
    QCache<QString, QByteArray> tile_cache_{200};
    QMutex cache_mutex_;
    QMutex dataset_mutex_;
    QList<GDALDataset*> dataset_pool_;
    QFuture<void> future_;
    QString gdal_data_path_;
    QString warped_path_;

    double ortho_progress_ {0.0};
    double ortho_opacity_ {0.75};
    QString ortho_status_ {};
    QString ortho_tile_url_ {};
    QString ortho_file_name_ {};
    bool ortho_ready_ {false};
    bool ortho_processing_ {false};
    bool ortho_cancelled_ {false};
};