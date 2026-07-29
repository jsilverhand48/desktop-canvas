// desktop-canvas-web: helper process rendering one web wallpaper.
//
// Spawned and supervised by the desktop-canvas daemon (one per output that
// shows a web wallpaper). Uses Qt WebEngine through Qt Quick: the top level
// is a QQuickView, which is a QWindow subclass, so LayerShellQt can be
// attached BEFORE the platform window exists. That ordering is mandatory:
// once a platform window is created, LayerShellQt silently falls back to
// its defaults (top layer, above all windows), which is exactly the failure
// mode a widget based QWebEngineView forces (it creates its platform
// window eagerly).
//
// Placement:
//   Wayland: layer shell background surface on the requested output
//            (--output NAME), anchored to all edges, keyboard off.
//   X11:     frameless window at --geometry XxYxWxH; the daemon demotes it
//            to the desktop layer by pid (_NET_WM_PID).
//
// Audio is muted unless --unmuted is passed (product default is muted). A
// user script injected at document creation provides the Wallpaper Engine
// JS hooks (wallpaperRegisterAudioListener etc.) as inert stubs and fires
// applyUserProperties({}) after load.
//
// Usage:
//   desktop-canvas-web --url file:///path/index.html
//       [--output DP-1 | --geometry 0x0x1920x1080] [--unmuted]
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQuickView>
#include <QScreen>
#include <QTemporaryFile>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/shell.h>
#include <LayerShellQt/window.h>
#endif

namespace {

// Root QML: one WebEngineView filling the window. The Wallpaper Engine
// shim script is built in C++ (it embeds the wallpaper's default
// properties) and handed in through the canvasShim context property.
// The context menu is suppressed: a wallpaper surface must never spawn
// popups over the desktop.
const char kQml[] = R"QML(
import QtQuick
import QtWebEngine

WebEngineView {
    anchors.fill: parent
    url: canvasUrl
    audioMuted: canvasMuted
    settings.playbackRequiresUserGesture: false
    settings.localContentCanAccessFileUrls: true
    settings.localContentCanAccessRemoteUrls: true
    onContextMenuRequested: function (request) { request.accepted = true; }
    userScripts.collection: [
        {
            name: "desktop-canvas-shim",
            sourceCode: canvasShim,
            injectionPoint: WebEngineScript.DocumentCreation,
            worldId: WebEngineScript.MainWorld
        }
    ]
}
)QML";

// Builds the injected script. propertiesJson is a JSON object shaped like
// Wallpaper Engine hands it to pages: { name: {"value": ...}, ... }.
QString buildShim(const QString& propertiesJson) {
    return QStringLiteral(R"JS(
window.wallpaperRegisterAudioListener = window.wallpaperRegisterAudioListener || function () {};
window.wallpaperRequestRandomFileForProperty = window.wallpaperRequestRandomFileForProperty || function () {};
window.addEventListener('load', function () {
  var l = window.wallpaperPropertyListener;
  if (l && typeof l.applyUserProperties === 'function') {
    try { l.applyUserProperties(%1); } catch (e) {}
  }
});
)JS")
        .arg(propertiesJson.isEmpty() ? QStringLiteral("{}") : propertiesJson);
}

// Reads general.properties from a project.json and converts it to the
// { name: {"value": ...} } object applyUserProperties expects.
QString loadProperties(const QString& projectPath) {
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    const auto props =
        doc.object()["general"].toObject()["properties"].toObject();
    QJsonObject out;
    for (auto it = props.begin(); it != props.end(); ++it) {
        const auto prop = it.value().toObject();
        if (prop.contains("value"))
            out[it.key()] = QJsonObject{{"value", prop["value"]}};
    }
    if (out.isEmpty()) return {};
    return QString::fromUtf8(
        QJsonDocument(out).toJson(QJsonDocument::Compact));
}

}  // namespace

int main(int argc, char** argv) {
    bool wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
#ifdef HAVE_LAYER_SHELL_QT
    if (wayland) LayerShellQt::Shell::useLayerShell();
#else
    if (wayland) {
        // Without LayerShellQt a Wayland window cannot be layered behind
        // the desktop; bail out instead of covering the user's screen.
        qWarning("desktop-canvas-web was built without LayerShellQt; "
                 "refusing to create an unlayered Wayland window");
        return 1;
    }
#endif
    // Wallpaper pages load their assets from file:// and autoplay media;
    // both need Chromium switches, appended to any user provided flags.
    QByteArray chromiumFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    chromiumFlags +=
        " --allow-file-access-from-files "
        "--autoplay-policy=no-user-gesture-required";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("desktop-canvas-web");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption urlOpt("url", "Wallpaper index.html URL", "url");
    QCommandLineOption outputOpt("output", "Output connector name", "name");
    QCommandLineOption geoOpt("geometry", "XxYxWxH window geometry", "geo");
    QCommandLineOption unmutedOpt("unmuted", "Play wallpaper audio");
    QCommandLineOption projectOpt("project",
                                  "project.json to read default properties",
                                  "path");
    parser.addOption(urlOpt);
    parser.addOption(outputOpt);
    parser.addOption(geoOpt);
    parser.addOption(unmutedOpt);
    parser.addOption(projectOpt);
    parser.process(app);
    if (!parser.isSet(urlOpt)) parser.showHelp(2);

    QScreen* target = nullptr;
    if (parser.isSet(outputOpt)) {
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == parser.value(outputOpt)) target = s;
    }
    if (!target) target = QGuiApplication::primaryScreen();

    QQuickView view;
    view.setColor(Qt::black);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setFlag(Qt::FramelessWindowHint);
    view.rootContext()->setContextProperty(
        "canvasUrl", QUrl(parser.value(urlOpt)));
    view.rootContext()->setContextProperty("canvasMuted",
                                           !parser.isSet(unmutedOpt));
    view.rootContext()->setContextProperty(
        "canvasShim", buildShim(loadProperties(parser.value(projectOpt))));

#ifdef HAVE_LAYER_SHELL_QT
    if (wayland) {
        // Must run before the platform window exists (before show()).
        auto* layer = LayerShellQt::Window::get(&view);
        layer->setLayer(LayerShellQt::Window::LayerBackground);
        layer->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
            LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope(QStringLiteral("desktop-canvas"));
        layer->setScreen(target);
    }
#endif
    if (!wayland) {
        if (parser.isSet(geoOpt)) {
            QStringList parts = parser.value(geoOpt).split('x');
            if (parts.size() == 4)
                view.setGeometry(parts[0].toInt(), parts[1].toInt(),
                                 parts[2].toInt(), parts[3].toInt());
        } else {
            view.setGeometry(target->geometry());
        }
    }

    // QQuickView::setSource needs a URL; write the embedded QML out to a
    // temporary file. The template is anchored to QDir::tempPath() because a
    // bare relative template resolves against the current working directory,
    // which litters wherever the daemon happened to be launched from (and
    // fails outright if that directory is read-only). The file auto-removes on
    // destruction, so only a hard kill of this process leaves one behind.
    QTemporaryFile qml(QDir::tempPath()
                       + QStringLiteral("/desktop-canvas-web-XXXXXX.qml"));
    if (!qml.open()) {
        qWarning("cannot create temporary QML file");
        return 1;
    }
    qml.write(kQml);
    qml.flush();
    view.setSource(QUrl::fromLocalFile(qml.fileName()));
    if (view.status() == QQuickView::Error) {
        for (const auto& err : view.errors())
            qWarning("%s", qPrintable(err.toString()));
        return 1;
    }
    view.show();
    return app.exec();
}
