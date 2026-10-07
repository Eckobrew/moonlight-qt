TEMPLATE = subdirs
SUBDIRS = \
    moonlight-common-c \
    qmdnsengine \
    app \
    h264bitstream

# Build the dependencies in parallel before the final app
app.depends = qmdnsengine moonlight-common-c h264bitstream
win32:!winrt {
    SUBDIRS += AntiHooking
    app.depends += AntiHooking
}

# Support debug and release builds from command line for CI
CONFIG += debug_and_release

# Run our compile tests
load(configure)
qtCompileTest(SL)
qtCompileTest(EGL)

macx {
    # 1. Force release optimizations and Link-Time Optimization
    CONFIG += release
    QMAKE_CXXFLAGS += -O3 -flto
    QMAKE_LFLAGS += -flto

    # 2. Hardened runtime & Metal / VideoToolbox framework links
    LIBS += -framework VideoToolbox -framework Metal -framework AVFoundation -framework CoreMedia

    # 3. Automate ad-hoc signing with network & HW-decoding entitlements
    ENTITLEMENTS_FILE = $$PWD/entitlements.plist
    QMAKE_POST_LINK += codesign --force --deep --entitlements $$ENTITLEMENTS_FILE --sign - $${TARGET}.app
}