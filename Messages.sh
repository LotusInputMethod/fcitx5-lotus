#!/bin/bash

xgettext \
--language=C++ \
--from-code=UTF-8 \
--keyword=_ \
--keyword=N_ \
--keyword=translate \
-o /tmp/lotus-cpp.pot \
$(find src settings-gui/cpp \( -name "*.cpp" -o -name "*.h" \) | sort)

xgettext \
--language=appdata \
--from-code=UTF-8 \
-o /tmp/lotus-xml.pot \
org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml.in.in

xgettext \
--language=JavaScript \
--from-code=UTF-8 \
--keyword=tr \
-o /tmp/lotus-qml.pot \
$(find settings-gui/qml -name "*.qml" | sort)

xgettext \
--language=Desktop \
--from-code=UTF-8 \
--keyword=Name \
--keyword=Comment \
-o /tmp/lotus-desktop.pot \
settings-gui/org.fcitx.Fcitx5.Addon.Lotus.Settings.desktop.in

{
    echo 'msgid ""'
    echo 'msgstr ""'
    echo '"Content-Type: text/plain; charset=UTF-8\n"'
    echo ""
    grep -hE "^Name=" \
    src/lotus.conf.in \
    src/lotus-addon.conf.in.in \
    | sed 's/^Name=\(.*\)/msgid "\1"\nmsgstr ""\n/'
} > /tmp/lotus-conf.pot

msgcat \
--use-first \
--sort-output \
/tmp/lotus-cpp.pot \
/tmp/lotus-xml.pot \
/tmp/lotus-conf.pot \
/tmp/lotus-qml.pot \
/tmp/lotus-desktop.pot \
-o po/fcitx5-lotus.pot

rm -f /tmp/lotus-*.pot
