#! /usr/bin/env bash
#! /usr/bin/env bash
$EXTRACT_TR_STRINGS $(find . -name "*.cpp" -o -name "*.h") -o $podir/phonon_gstreamer_qt.pot
