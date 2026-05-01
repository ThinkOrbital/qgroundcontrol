#include "OrthoBackend.h"

#include <QtConcurrent>

OrthomosaicBackend::OrthomosaicBackend(QObject* parent)
    : QObject(parent)
{
       GDALAllRegister();
}

/********************* Orthomosiac**************************** */

//load GeoTiff
void OrthomosaicBackend::loadGeoTiff(const QString& path)
{   
    if(this->ortho_processing_) return;

    this->ortho_cancelled_ = false;
    this->ortho_ready_ = false;
    this->ortho_processing_ = true;
    this->ortho_progress_ = 0.0;
    emit orthoProcessingChanged();
    emit orthoReadyChanged();

    QString cleaned = path;
    if (cleaned.startsWith("file://"))
        cleaned = QUrl(path).toLocalFile();

    this->future_ = QtConcurrent::run([this, cleaned]()
    {
        if (!this->stepValidateAndOpen(cleaned)) return;

        this->ortho_file_name_ = QFileInfo(cleaned).fileName();
        emit orthoFileNameChanged();

        if (this->ortho_cancelled_) return;
        if (!this->stepReprojectToWebMercator()) return;

        if (this->ortho_cancelled_) return;
        if (!this->stepBuildOverviews()) return;

        if (this->ortho_cancelled_) return;
        if (!this->stepStartTileServer()) return;

        this->ortho_ready_ = true;
        this->ortho_processing_ = false;

        emit orthoReadyChanged();
        emit orthoProcessingChanged();
    });
}

// Step 1: Validate and Open GeoTiff
bool OrthomosaicBackend::stepValidateAndOpen(const QString& path)
{
    setOrthoProgress(0.0, "Opening GeoTIFF...");

    this->gdal_data_path_ = path;

    // Clean previous dataset safely (no Qt invoke hacks)
    cleanupPreviousSession();

    const std::string nativePath = path.toUtf8().toStdString();

    gdal_dataset_ = static_cast<GDALDataset*>(
        GDALOpen(nativePath.c_str(), GA_ReadOnly)
    );

    if (!gdal_dataset_)
    {
        emit errorOccurred("Failed to open GeoTIFF: " + path);
        return false;
    }

    // --- Projection check
    const char* proj = gdal_dataset_->GetProjectionRef();
    if (!proj || strlen(proj) == 0)
    {
        emit errorOccurred("GeoTIFF has no projection.");
        cleanupPreviousSession();
        return false;
    }

    // --- Geotransform check
    double gt[6];
    if (gdal_dataset_->GetGeoTransform(gt) != CE_None)
    {
        emit errorOccurred("GeoTIFF missing geotransform.");
        cleanupPreviousSession();
        return false;
    }

    // --- Band check
    const int bands = gdal_dataset_->GetRasterCount();
    if (bands < 3)
    {
        emit errorOccurred("GeoTIFF must have at least 3 bands (RGB).");
        cleanupPreviousSession();
        return false;
    }

    setOrthoProgress(5.0, "GeoTIFF validated");

    return true;
}   

// Step 2: Reproject to Web Mercator (EPSG:3857)
bool OrthomosaicBackend::stepReprojectToWebMercator()
{
    setOrthoProgress(5.0, "Warping to EPSG:3857...");

    if (!gdal_dataset_)
    {
        emit errorOccurred("No dataset loaded.");
        return false;
    }

    QString inputPath = gdal_data_path_;

    warped_path_ = QDir::tempPath() + "/ortho_3857.tif";

    QStringList args;

    args << "-t_srs" << "EPSG:3857"
         << "-r" << "bilinear"
         << "-of" << "GTiff"

         // VERY IMPORTANT: grid alignment
         << "-tap"

         // tiling
         << "-co" << "TILED=YES"
         << "-co" << "BLOCKXSIZE=512"
         << "-co" << "BLOCKYSIZE=512"

         // compression
         << "-co" << "COMPRESS=LZW"

         // input/output
         << inputPath
         << warped_path_;

    QProcess* proc = new QProcess(this);

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]()
    {
        QByteArray err = proc->readAllStandardError();
        qWarning() << "[gdalwarp]" << err;
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus status)
    {
        proc->deleteLater();

        if (code != 0 || status != QProcess::NormalExit)
        {
            emit errorOccurred("gdalwarp failed");
            return;
        }

        this->setOrthoProgress(40.0, "Warp complete");

        // NOW safe to open dataset
        this->warped_dataset_ =
            (GDALDataset*)GDALOpen(warped_path_.toStdString().c_str(), GA_ReadOnly);

        if (!this->warped_dataset_)
        {
            emit errorOccurred("Failed to reopen warped dataset");
            return;
        }

        this->stepBuildOverviews();
    });

    proc->start("gdalwarp", args);

    return true;
}

// Step 3: Build overviews (zoom levels)
bool OrthomosaicBackend::stepBuildOverviews()
{
    setOrthoProgress(40.0, "Building overviews (gdaladdo)...");

    if (warped_path_.isEmpty())
    {
        emit errorOccurred("No warped dataset available.");
        return false;
    }

    QStringList args;

    args << "-r" << "lanczos"
         << warped_path_
         << "2" << "4" << "8" << "16" << "32";

    QProcess* proc = new QProcess(this);

    connect(proc, &QProcess::readyReadStandardError, this, [proc]()
    {
        qWarning() << "[gdaladdo]" << proc->readAllStandardError();
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus status)
    {
        proc->deleteLater();

        if (code != 0 || status != QProcess::NormalExit)
        {
            emit errorOccurred("gdaladdo failed");
            return;
        }

        setOrthoProgress(70.0, "Overviews built");

        // IMPORTANT: reopen dataset so overviews are visible
        if (warped_dataset_)
        {
            GDALClose(warped_dataset_);
            warped_dataset_ = nullptr;
        }

        warped_dataset_ =
            (GDALDataset*)GDALOpen(warped_path_.toStdString().c_str(), GA_ReadOnly);

        if (!warped_dataset_)
        {
            emit errorOccurred("Failed to reopen warped dataset after overviews");
            return;
        }

        stepStartTileServer();
    });

    proc->start("gdaladdo", args);

    return true;
}

// Step 5: Start tile server
bool OrthomosaicBackend::stepStartTileServer()
{
   bool success = false;

    QMetaObject::invokeMethod(this, [this, &success]() {

        this->qtcp_server_ = new QTcpServer(this);

        connect(this->qtcp_server_, &QTcpServer::newConnection,
                this, [this]() {

            while (this->qtcp_server_->hasPendingConnections()) {

                QTcpSocket* socket = this->qtcp_server_->nextPendingConnection();

                qDebug() << "Tile client connected from"
                         << socket->peerAddress().toString();

                socket_buffers_[socket] = {};

                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket]() {

                    socket_buffers_[socket] += socket->readAll();

                    QByteArray& buffer = socket_buffers_[socket];

                    if (!buffer.contains("\r\n\r\n"))
                        return;

                    this->handleTileRequest(socket, buffer);

                    socket_buffers_.remove(socket);
                });

                connect(socket, &QTcpSocket::disconnected,
                        this, [this, socket]() {
                    socket_buffers_.remove(socket);
                    socket->deleteLater();
                });
            }
        });

        if (!this->qtcp_server_->listen(QHostAddress::LocalHost, 0)) {
            qWarning() << "Tile server failed:" << this->qtcp_server_->errorString();
            success = false;
            return;
        }

        this->ortho_tile_url_ =
            QString("http://localhost:%1/tiles/{z}/{x}/{y}.png")
                .arg(this->qtcp_server_->serverPort());

        qDebug() << "Tile server running at:" << this->ortho_tile_url_;

        success = true;

    }, Qt::BlockingQueuedConnection);

    if (!success) return false;

    this->setOrthoProgress(100.0, "Ready ✓");
    return true;
}

void OrthomosaicBackend::handleTileRequest(QTcpSocket* socket,
                                           const QByteArray& request)
{
    QByteArray line = request.left(request.indexOf("\r\n"));

    QRegularExpression re(
        R"(^GET /tiles/(\d+)/(\d+)/(\d+)\.png HTTP/1\.[01])"
    );

    QRegularExpressionMatch match = re.match(QString::fromLatin1(line));

    if (!match.hasMatch()) {
        socket->write(
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n\r\n"
        );
        socket->disconnectFromHost();
        return;
    }

    int z = match.captured(1).toInt();
    int x = match.captured(2).toInt();
    int y = match.captured(3).toInt();

    QThreadPool::globalInstance()->start([this, socket, z, x, y]() {

        QByteArray tile = this->renderTile(z, x, y);

        QMetaObject::invokeMethod(this, [socket, tile]() {

            if (!socket || socket->state() != QAbstractSocket::ConnectedState)
                return;

            QByteArray header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/png\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Cache-Control: max-age=3600\r\n"
                "Content-Length: " + QByteArray::number(tile.size()) + "\r\n\r\n";

            socket->write(header);
            socket->write(tile);
            socket->disconnectFromHost();

        }, Qt::QueuedConnection);
    });
}

QByteArray OrthomosaicBackend::renderTile(int z, int x, int y)
{
    const QString key = QString("%1/%2/%3").arg(z).arg(x).arg(y);

    {
        QMutexLocker lock(&cache_mutex_);
        if (tile_cache_.contains(key))
            return *tile_cache_.object(key);
    }

    QByteArray tile = renderTileFromGDAL(z, x, y);

    {
        QMutexLocker lock(&cache_mutex_);
        tile_cache_.insert(key, new QByteArray(tile));
    }

    return tile;
}

QByteArray OrthomosaicBackend::renderTileFromGDAL(int z, int x, int y)
{

    
    if (!this->warped_dataset_)
    {
        qWarning() << "Warped dataset does not exist!";
        return {};
    }

    // qWarning() << "CRS:"
    //        << this->warped_dataset_->GetProjectionRef();

    // qWarning() << "Dataset size:"
    //        << this->warped_dataset_->GetRasterXSize()
    //        << this->warped_dataset_->GetRasterYSize();

      // --- 1. Get GeoTransform (pixel <-> meters)
    double gt[6];
    if (this->warped_dataset_->GetGeoTransform(gt) != CE_None)
    {
        qWarning() << "No geotransform!";
        return {};
    }

    // Inverse transform (meters -> pixel)
    double invGT[6];
    if (!GDALInvGeoTransform(gt, invGT))
    {
        qWarning() << "Failed to invert geotransform";
        return {};
    }

    // qWarning() << "gt = " << gt[0] << gt[1] << gt[2]
    //                 << gt[3] << gt[4] << gt[5];


    // qWarning() << "invGT = " << invGT[0] << invGT[1] << invGT[2]
    //                 << invGT[3] << invGT[4] << invGT[5];


    const int TILE = 256;
    std::vector<uint8_t> buf(TILE * TILE * 4, 0);

    int rasterX = this->warped_dataset_->GetRasterXSize();
    int rasterY = this->warped_dataset_->GetRasterYSize();

    const double originShift = 20037508.342789244;
    const double n = std::pow(2.0, z);

    double minX = (x / n) * 2.0 * originShift - originShift;
    double maxX = ((x + 1) / n) * 2.0 * originShift - originShift;

    double maxY = originShift - (y / n) * 2.0 * originShift;
    double minY = originShift - ((y + 1) / n) * 2.0 * originShift;

    qWarning() << "X min and max: " << minX << maxX;
    qWarning() << "Y min and max: " << minY << maxY;

    // Dataset extent
    double dsWest  = gt[0];
    double dsEast  = gt[0] + rasterX * gt[1];
    double dsNorth = gt[3];
    double dsSouth = gt[3] + rasterY * gt[5];

    qWarning() << "dsWest, dsEast, dsNorth, dsSouth: " << dsWest << dsEast << dsNorth << dsSouth;

//     // // Early exit if no intersection
//     // if (maxX <= dsWest  || minX >= dsEast ||
//     //     maxY <= dsSouth || minY >= dsNorth)
//     // {
//     //     qWarning() << "Returning because no intersection";
//     //     return {};
//     // }

//     auto worldToPixel = [&](double mx, double my, double& px, double& py)
//     {
//         GDALApplyGeoTransform(invGT, mx, my, &px, &py);
//     };  

//     /* Get Boundary */
//     double px0, py0, px1, py1;

//     // top-left
//     worldToPixel(minX, maxY, px0, py0);

//     // bottom-right
//     worldToPixel(maxX, minY, px1, py1);

//     qWarning() << "Top Left Bounds: " << px0 << py0;
//     qWarning() << "Bottom Right Bounds: " << px1 << py1;

//     /*Get Raster Window*/
//     int srcX = static_cast<int>(std::floor(std::min(px0, px1)));
//     int srcY = static_cast<int>(std::floor(std::min(py0, py1)));

//     int srcW = static_cast<int>(std::ceil(std::abs(px1 - px0)));
//     int srcH = static_cast<int>(std::ceil(std::abs(py1 - py0)));

//     qWarning() << "Raster window x, y, w, h = " << srcX << srcY << srcW << srcH;

//    // Clamp to dataset bounds
//     if (srcX < 0) { srcW += srcX; srcX = 0; }
//     if (srcY < 0) { srcH += srcY; srcY = 0; }
//     if (srcX + srcW > rasterX) srcW = rasterX - srcX;
//     if (srcY + srcH > rasterY) srcH = rasterY - srcY;

//      qWarning() << "Raster window after clamp x, y, w, h = " << srcX << srcY << srcW << srcH;

//     if (srcW <= 0 || srcH <= 0)
//         return {};

   

//     int bands[3] = {1,2,3};
//     int bandCount = std::min(this->warped_dataset_->GetRasterCount(), 3);

//     CPLErr err = this->warped_dataset_->RasterIO(
//         GF_Read,
//         srcX, srcY, srcW,srcH,
//         buf.data(),
//         256, 256,
//         GDT_Byte,
//         3, bands,
//         4, 256*4, 1
//     );



//     if (err != CE_None) {
//         qWarning() << "RasterIO failed";
//         // releaseDataset(this->warped_dataset_)
//         return {};
//     }

//     if (bandCount == 3)
//         for (int i = 0; i < TILE * TILE; i++)
//             buf[i * 4 + 3] = 255;

//     QImage img(buf.data(), TILE, TILE, TILE * 4, QImage::Format_RGBA8888);
//     img = img.copy();

    QByteArray out;
//     QBuffer b(&out);
//     b.open(QIODevice::WriteOnly);
//     img.save(&b, "PNG");

    // releaseDataset(this->warped_dataset_);
    return out;
}

GDALDataset* OrthomosaicBackend::acquireDataset()
{
    QMutexLocker lock(&this->dataset_mutex_);

    if (!this->warped_dataset_) {
        qWarning() << "No active warped GDAL dataset";
        return nullptr;
    }

    return this->warped_dataset_;
}

void OrthomosaicBackend::releaseDataset(GDALDataset* ds)
{
    QMutexLocker lock(&this->dataset_mutex_);
    GDALClose(ds);
    // this->dataset_pool_.append(ds);
}

void OrthomosaicBackend::cleanupPreviousSession()
{
    // Ensure main thread execution
    if (QThread::currentThread() != qApp->thread())
    {
        QMetaObject::invokeMethod(
            this,
            "cleanupPreviousSession",
            Qt::QueuedConnection
        );
        return;
    }

    ortho_cancelled_ = true;

    // --- Stop tile server safely
    if (qtcp_server_)
    {
        qtcp_server_->close();
        qtcp_server_->disconnect();
        qtcp_server_->deleteLater();
        qtcp_server_ = nullptr;
    }

    // --- Protect dataset teardown
    {
        QMutexLocker lock(&dataset_mutex_);

        if (gdal_dataset_)
        {
            GDALClose(gdal_dataset_);
            gdal_dataset_ = nullptr;
        }

        if (warped_dataset_)
        {
            GDALClose(warped_dataset_);
            warped_dataset_ = nullptr;
        }
    }

    // --- Cache cleanup
    {
        QMutexLocker lock(&cache_mutex_);
        tile_cache_.clear();
    }

    // --- Clear socket buffers
    socket_buffers_.clear();

    // --- Remove temp files (safer pattern)
    const QString base = QDir::tempPath() + "/ortho_" + QString::number(reinterpret_cast<uintptr_t>(this));

    const QStringList tmpFiles = {
        base + "_3857.tif",
        base + "_cog.tif"
    };

    for (const QString& f : tmpFiles)
    {
        if (QFile::exists(f))
            QFile::remove(f);
    }

    ortho_tile_url_.clear();
}

void OrthomosaicBackend::setOrthoProgress(double prog, const QString& status)
{
    this->ortho_progress_ = prog;
    this->ortho_status_   = status;
    // Marshal back to main thread for QML
    QMetaObject::invokeMethod(this, [this]() {
        emit orthoProgressChanged();
        emit orthoStatusChanged();
    }, Qt::QueuedConnection);
}