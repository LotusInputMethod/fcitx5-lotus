#!/bin/bash

xgettext \
--language=C++ \
--from-code=UTF-8 \
--keyword=_ \
--keyword=N_ \
-o /tmp/lotus-cpp.pot \
$(find . \( -name "*.cpp" -o -name "*.h" \))

xgettext \
--language=appdata \
--from-code=UTF-8 \
-o /tmp/lotus-xml.pot \
org.fcitx.Fcitx5.Addon.Lotus.metainfo.xml.in

xgettext \
--language=Python \
--from-code=UTF-8 \
--keyword=_ \
-o /tmp/lotus-python.pot \
$(find . -name "*.py")

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
/tmp/lotus-cpp.pot \
/tmp/lotus-xml.pot \
/tmp/lotus-conf.pot \
/tmp/lotus-python.pot \
/tmp/lotus-desktop.pot \
-o po/fcitx5-lotus.pot
