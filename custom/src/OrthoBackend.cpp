#include "OrthoBackend.h"

#include <QtConcurrent>
#include <QImage>
#include <QBuffer>

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


    if (!this->stepValidateAndOpen(cleaned))
    {
        qWarning() << "Validate and Open step failed";
            return;
    }
    qDebug() << "Validate and Open was successful";

    this->ortho_file_name_ = QFileInfo(cleaned).fileName();
    emit orthoFileNameChanged();

    qDebug() << "Ortho filename changed successfully";
    if (this->ortho_cancelled_) return;

    qDebug() << "Reprojecting to Web Mercator...";
    if (!this->stepReprojectToWebMercator()) return;
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

    // warped_path_ = QDir::tempPath() + "/ortho_3857.tif";
    warped_path_ = QDir::tempPath() +
    QString("/ortho_%1_3857.tif")
        .arg(reinterpret_cast<uintptr_t>(this));

    QStringList args;

    args << "-t_srs" << "EPSG:3857"
         << "-r" << "bilinear"
         << "-of" << "GTiff"

         // VERY IMPORTANT: grid alignment
        //  << "-tap"

         // tiling
         << "-co" << "TILED=YES"
         << "-co" << "BLOCKXSIZE=512"
         << "-co" << "BLOCKYSIZE=512"

         // compression
         << "-co" << "COMPRESS=LZW"

         // input/output
         << inputPath
         << warped_path_;


    QProcess* proc = new QProcess();

    qDebug() << "Running gdalwarp with args:" << args;
    qDebug() << "Running gdalwarp:" << QStandardPaths::findExecutable("gdalwarp");
    qDebug() << "Input path exists:" << QFile::exists(inputPath);
    qDebug() << "Output path:" << warped_path_;
    
    connect(proc, &QProcess::started, this, []() {
        qDebug() << "gdalwarp process started successfully";
    });

    connect(proc, &QProcess::readyReadStandardOutput, proc, [this, proc]() {
        QString out = proc->readAllStandardOutput();
        qDebug() << "[gdalwarp stdout]" << out;
        
        // gdalwarp outputs "0...10...20...30...40...50...60...70...80...90...100"
        QRegularExpression re(R"((\d+)\.\.\.)");
        auto it = re.globalMatch(out);
        int lastVal = -1;
        while (it.hasNext())
            lastVal = it.next().captured(1).toInt();

        if (lastVal >= 0) {
            // Map gdalwarp 0-100 into our 5-40% progress range
            double mapped = 5.0 + (lastVal / 100.0) * 35.0;
            setOrthoProgress(mapped, "Warping to EPSG:3857...");
        }
    });

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]()
    {
        QByteArray err = proc->readAllStandardError();
        qWarning() << "[gdalwarp]" << err;
    });

    connect(proc, &QProcess::errorOccurred, this, [](QProcess::ProcessError err) {
        qWarning() << "gdalwarp process error:" << err;
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus status)
    {
        if(this->ortho_cancelled_) return;

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

    if (!proc->waitForStarted(3000)) {
        emit errorOccurred("gdalwarp failed to start — is it in PATH?");
        return false;
    }
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
        if(this->ortho_cancelled_) return;

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

        // After reopening warped_dataset_ in the finished lambda
        double gt[6];
        warped_dataset_->GetGeoTransform(gt);

        int width = warped_dataset_->GetRasterXSize();
        int height = warped_dataset_->GetRasterYSize();

        double minX = gt[0];
        double maxY = gt[3];
        double maxX = gt[0] + width * gt[1];
        double minY = gt[3] + height * gt[5];

        qDebug() << "Warped dataset bounds (EPSG:3857):";
        qDebug() << "  minX:" << minX << "maxX:" << maxX;
        qDebug() << "  minY:" << minY << "maxY:" << maxY;

        // Convert to lat/lon for sanity check
        double centerLon = ((minX + maxX) / 2.0) / 20037508.34 * 180.0;
        double centerLat = std::atan(std::exp(((minY + maxY) / 2.0) / 20037508.34 * M_PI)) * 360.0 / M_PI - 90.0;
        qDebug() << "Center lat/lon:" << centerLat << centerLon;

        auto lonToTileX = [](double lon, int z) -> int {
            return (int)std::floor((lon + 180.0) / 360.0 * std::pow(2.0, z));
        };
        auto latToTileY = [](double lat, int z) -> int {
            double latRad = lat * M_PI / 180.0;
            return (int)std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * std::pow(2.0, z));
        };

        for (int z : {15, 16, 17, 18}) {
            int tx = lonToTileX(centerLon, z);
            int ty = latToTileY(centerLat, z);
            qDebug() << "Zoom" << z << "-> tile" << tx << ty;
            qDebug() << "  curl -o tile.png \"http://localhost:33679/tiles/" 
                    << z << "/" << tx << "/" << ty << ".png\"";
        }

        stepStartTileServer();
    });

    proc->start("gdaladdo", args);

    return true;
}

// Step 5: Start tile server
bool OrthomosaicBackend::stepStartTileServer()
{
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
        return false;
    }

    this->ortho_tile_url_ =
        QString("http://localhost:%1/tiles/{z}/{x}/{y}.png")
            .arg(this->qtcp_server_->serverPort());

    this->ortho_ready_ = true;
    qDebug() << "Tile server running at:" << this->ortho_tile_url_;

    emit orthoReadyChanged();

    this->ortho_processing_ = false;
    this->setOrthoProgress(100.0, "Ready ✓");
    return true;
}

void OrthomosaicBackend::handleTileRequest(QTcpSocket* socket,
                                           const QByteArray& request)
{
    // --- Parse XYZ from request line, e.g. "GET /tiles/12/2134/1455.png HTTP/1.1"
    QString req = QString::fromUtf8(request);
    QRegularExpression re(R"(/tiles/(\d+)/(\d+)/(\d+)\.png)");
    auto match = re.match(req);

    if (!match.hasMatch()) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
        return;
    }

    int z = match.captured(1).toInt();
    int x = match.captured(2).toInt();
    int y = match.captured(3).toInt();

    qDebug() << "x: " << x << " y:" << y << " z:" << z; 

    // --- Convert XYZ tile to EPSG:3857 bounds
    // Standard Web Mercator tile math
    double n = std::pow(2.0, z);
    double originShift = 2 * M_PI * 6378137.0 / 2.0; // ~20037508.34
    double tileSize = 2 * originShift / n;

    double minX = x * tileSize - originShift;
    double maxX = minX + tileSize;
    double maxY = originShift - y * tileSize;
    double minY = maxY - tileSize;


    // --- Map mercator bounds to pixel coords in warped_dataset_
    double gt[6];
    warped_dataset_->GetGeoTransform(gt);

    // qDebug() << "Dataset gt[0]=" << gt[0] << "gt[3]=" << gt[3]
    //      << "gt[1]=" << gt[1] << "gt[5]=" << gt[5];


    int rawX = static_cast<int>((minX - gt[0]) / gt[1]);
    int rawY = static_cast<int>((maxY - gt[3]) / gt[5]); // gt[5] negative → positive pixel offset
    int rawW = static_cast<int>((maxX - minX)  / gt[1]);
    int rawH = static_cast<int>((minY - maxY)  / gt[5]); // both negative → positive result

    qDebug() << "Raw pixel window: x=" << rawX << "y=" << rawY
         << "w=" << rawW << "h=" << rawH;

    int dsWidth  = warped_dataset_->GetRasterXSize();
    int dsHeight = warped_dataset_->GetRasterYSize();

    qDebug() << "Raster size dsWidth = " << dsWidth << " dsHeight = " << dsHeight;

      // Check if tile is completely outside raster
    if (rawX >= dsWidth  || rawY >= dsHeight ||
        rawX + rawW <= 0 || rawY + rawH <= 0)
    {
        qDebug() << "Tile outside raster extent, sending empty tile";
        sendEmptyTile(socket);
        return;
    }

    // // If the source window is larger than the whole dataset, this zoom is too low
    // if (rasterW > dsWidth || rasterH > dsHeight) {
    //     qDebug() << "Source window is larger than the whole dataset, this zoom is too low";
    //     sendEmptyTile(socket);
    //     return;
    // }

    // Clamp to raster bounds
    int clampedX = std::max(0, rawX);
    int clampedY = std::max(0, rawY);
    int clampedW = std::min(rawW, dsWidth  - clampedX);
    int clampedH = std::min(rawH, dsHeight - clampedY);

    qDebug() << "Clamped pixel window: x=" << clampedX << "y=" << clampedY
         << "w=" << clampedW << "h=" << clampedH;

    // --- Read RGB via RasterIO (resamples to 256x256 using overviews automatically)
    const int TILE_SIZE = 256;
    int outX = static_cast<int>((clampedX - rawX) * TILE_SIZE / (double)rawW);
    int outY = static_cast<int>((clampedY - rawY) * TILE_SIZE / (double)rawH);
    int outW = static_cast<int>(clampedW * TILE_SIZE / (double)rawW);
    int outH = static_cast<int>(clampedH * TILE_SIZE / (double)rawH);

    // Clamp output dimensions
    outW = std::min(outW, TILE_SIZE - outX);
    outH = std::min(outH, TILE_SIZE - outY);

    if (outW <= 0 || outH <= 0)
    {
        sendEmptyTile(socket);
        return;
    }

    // --- RGBA buffer (transparent black by default)
    std::vector<uint8_t> buf(TILE_SIZE * TILE_SIZE * 4, 0);

    // --- Temp buffer for the clamped read
    std::vector<uint8_t> readBuf(outW * outH * 3);

    // --- RasterIO — GDAL picks overview automatically based on outW/outH vs clampedW/clampedH
    int bandMap[3] = {1, 2, 3};
    CPLErr err = warped_dataset_->RasterIO(
        GF_Read,
        clampedX, clampedY, clampedW, clampedH,  // source window (clamped)
        readBuf.data(), outW, outH,               // output size (GDAL downsamples via overviews)
        GDT_Byte,
        3, bandMap,
        3, outW * 3, 1,                           // pixel, line, band spacing
        nullptr
    );

    if (err != CE_None) {
        // Tile is outside raster extent — return transparent PNG
        qWarning() << "RasterIO failed for tile" << z << x << y;
        sendEmptyTile(socket);
        return;
    }

    // --- Copy readBuf into the correct position in the RGBA tile buffer
    for (int row = 0; row < outH; row++)
    {
        for (int col = 0; col < outW; col++)
        {
            int srcIdx = (row * outW + col) * 3;
            int dstIdx = ((outY + row) * TILE_SIZE + (outX + col)) * 4;
            buf[dstIdx + 0] = readBuf[srcIdx + 0]; // R
            buf[dstIdx + 1] = readBuf[srcIdx + 1]; // G
            buf[dstIdx + 2] = readBuf[srcIdx + 2]; // B
            buf[dstIdx + 3] = 255;                 // A — fully opaque
        }
    }

    // --- Encode to PNG
    QImage img(buf.data(), TILE_SIZE, TILE_SIZE, TILE_SIZE * 4, QImage::Format_RGBA8888);
    QByteArray png;
    QBuffer pngBuf(&png);
    pngBuf.open(QIODevice::WriteOnly);
    img.save(&pngBuf, "PNG");

    // --- Write HTTP response
    QByteArray response;
    response  = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: image/png\r\n";
    response += "Content-Length: " + QByteArray::number(png.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    response += png;

    socket->write(response);
    socket->flush();
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

void OrthomosaicBackend::sendEmptyTile(QTcpSocket* socket)
{
    QByteArray png(reinterpret_cast<const char*>(kEmptyPng), sizeof(kEmptyPng));

    QByteArray response;
    response  = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: image/png\r\n";
    response += "Content-Length: " + QByteArray::number(png.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Cache-Control: public, max-age=86400\r\n"; // tiles outside extent never change
    response += "\r\n";
    response += png;

    socket->write(response);
    socket->flush();
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
    ortho_cancelled_ = false;
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