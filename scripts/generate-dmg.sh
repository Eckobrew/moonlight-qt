# This script requires create-dmg to be installed from https://github.com/sindresorhus/create-dmg
BUILD_CONFIG=$1

fail()
{
	echo "$1" 1>&2
	exit 1
}

if [ "$BUILD_CONFIG" != "Debug" ] && [ "$BUILD_CONFIG" != "Release" ]; then
  fail "Invalid build configuration - expected 'Debug' or 'Release'"
fi

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

if [ "$SIGNING_PROVIDER_SHORTNAME" == "" ]; then
  SIGNING_PROVIDER_SHORTNAME=$SIGNING_IDENTITY
fi
if [ "$SIGNING_IDENTITY" == "" ]; then
  SIGNING_IDENTITY=$SIGNING_PROVIDER_SHORTNAME
fi

[ "$SIGNING_IDENTITY" == "" ] || git diff-index --quiet HEAD -- || fail "Signed release builds must not have unstaged changes!"

echo Updating dependencies
python3 $SOURCE_ROOT/setup-deps.py

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $INSTALLER_FOLDER
rm -rf $SOURCE_ROOT/app/release $SOURCE_ROOT/app/debug $SOURCE_ROOT/app/Makefile* $SOURCE_ROOT/app/Moonlight.app $SOURCE_ROOT/app/qml_qmlcache.qrc
mkdir -p $BUILD_ROOT
mkdir -p $BUILD_FOLDER
mkdir -p $INSTALLER_FOLDER

# Enable LTO for official builds
export CFLAGS=-flto=thin
export CXXFLAGS=-flto=thin
export LDFLAGS=-flto=thin

# Determine target architectures
if [ -z "$QMAKE_APPLE_DEVICE_ARCHS" ]; then
  if [ -n "$CI" ] || [ -n "$CI_VERSION" ] || [ -n "$GITHUB_ACTIONS" ]; then
    QMAKE_APPLE_DEVICE_ARCHS="x86_64 arm64"
  else
    QMAKE_APPLE_DEVICE_ARCHS="$(uname -m)"
  fi
fi

echo Configuring the project for $QMAKE_APPLE_DEVICE_ARCHS
pushd $BUILD_FOLDER
qmake $SOURCE_ROOT/moonlight-qt.pro QMAKE_APPLE_DEVICE_ARCHS="$QMAKE_APPLE_DEVICE_ARCHS" || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(sysctl -n hw.logicalcpu) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Saving dSYM file
pushd $BUILD_FOLDER
dsymutil app/Moonlight.app/Contents/MacOS/Moonlight -o Moonlight-$VERSION.dsym || fail "dSYM creation failed!"
cp -R Moonlight-$VERSION.dsym $INSTALLER_FOLDER || fail "dSYM copy failed!"
popd

echo Creating app bundle
EXTRA_ARGS=
if [ "$BUILD_CONFIG" == "Debug" ]; then EXTRA_ARGS="$EXTRA_ARGS -use-debug-libs"; fi
echo Extra deployment arguments: $EXTRA_ARGS
macdeployqt $BUILD_FOLDER/app/Moonlight.app $EXTRA_ARGS -qmldir=$SOURCE_ROOT/app/gui -appstore-compliant || fail "macdeployqt failed!"

echo Removing dSYM files from app bundle
find $BUILD_FOLDER/app/Moonlight.app/ -name '*.dSYM' | xargs rm -rf

if [ "$SIGNING_IDENTITY" != "" ]; then
  echo Signing app bundle
  codesign --force --deep --options runtime --timestamp --sign "$SIGNING_IDENTITY" $BUILD_FOLDER/app/Moonlight.app || fail "Signing failed!"
fi

echo Creating DMG
if command -v create-dmg >/dev/null 2>&1 && create-dmg --help 2>&1 | grep -q "<output_name.dmg>"; then
  # create-dmg/create-dmg (Homebrew / MacPorts bash version)
  DMG_STAGING="$BUILD_FOLDER/dmg-staging"
  rm -rf "$DMG_STAGING"
  mkdir -p "$DMG_STAGING"
  cp -R "$BUILD_FOLDER/app/Moonlight.app" "$DMG_STAGING/"
  if [ "$SIGNING_IDENTITY" != "" ]; then
    create-dmg --codesign "$SIGNING_IDENTITY" --sandbox-safe "$INSTALLER_FOLDER/Moonlight.dmg" "$DMG_STAGING" || fail "create-dmg failed!"
  else
    create-dmg --sandbox-safe "$INSTALLER_FOLDER/Moonlight.dmg" "$DMG_STAGING"
    case $? in
      0) ;;
      2) ;;
      *) fail "create-dmg failed!";;
    esac
  fi
  rm -rf "$DMG_STAGING"
elif command -v create-dmg >/dev/null 2>&1; then
  # sindresorhus/create-dmg (npm version used in CI)
  if [ "$SIGNING_IDENTITY" != "" ]; then
    create-dmg $BUILD_FOLDER/app/Moonlight.app $INSTALLER_FOLDER --identity="$SIGNING_IDENTITY" --no-version-in-filename || fail "create-dmg failed!"
  else
    create-dmg $BUILD_FOLDER/app/Moonlight.app $INSTALLER_FOLDER --no-version-in-filename
    case $? in
      0) ;;
      2) ;;
      *) fail "create-dmg failed!";;
    esac
  fi
else
  echo "create-dmg not installed, creating disk image with hdiutil"
  DMG_STAGING="$BUILD_FOLDER/dmg-staging"
  rm -rf "$DMG_STAGING"
  mkdir -p "$DMG_STAGING"
  cp -R "$BUILD_FOLDER/app/Moonlight.app" "$DMG_STAGING/"
  hdiutil create -volname "Moonlight" -srcfolder "$DMG_STAGING" -ov -format UDZO "$INSTALLER_FOLDER/Moonlight.dmg" || fail "hdiutil failed!"
  rm -rf "$DMG_STAGING"
  if [ "$SIGNING_IDENTITY" != "" ]; then
    codesign --force --sign "$SIGNING_IDENTITY" "$INSTALLER_FOLDER/Moonlight.dmg" || fail "Signing failed!"
  fi
fi

if [ "$NOTARY_KEYCHAIN_PROFILE" != "" ]; then
  echo Uploading to App Notary service
  xcrun notarytool submit --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait $INSTALLER_FOLDER/Moonlight.dmg || fail "Notary submission failed"

  echo Stapling notary ticket to DMG
  xcrun stapler staple -v $INSTALLER_FOLDER/Moonlight.dmg || fail "Notary ticket stapling failed!"
fi

mv $INSTALLER_FOLDER/Moonlight.dmg $INSTALLER_FOLDER/Moonlight-$VERSION.dmg
echo Build successful