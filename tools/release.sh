#!/bin/bash
# Folio Gable release checks: unit tests, the click-everything audit and desktop-theme runs (source build),
# deb, the same on the installed package in jammy and nolo. Display :96; full audit under the heavy lock; Wayland desktop-theme pass on 24.04.
set -u
R=/home/claude/gable
V=$(grep -oP 'project\(Gable VERSION \K[0-9.]+' $R/CMakeLists.txt)
/home/claude/mounts.sh
pgrep -f "Xvfb :96" >/dev/null || { rm -f /tmp/.X96-lock /tmp/.X11-unix/X96; setsid -f Xvfb :96 -screen 0 1600x1000x24 -nolisten tcp </dev/null >/dev/null 2>&1; sleep 1; }
rsync -a --delete --exclude .git --exclude build --exclude build-release $R/ /srv/jammy/src/gable/
echo "== build + unit tests (22.04)"
chroot /srv/jammy bash -c "cd /src/gable && ./packaging/build-deb.sh /src > /tmp/gable_build.log 2>&1; tail -2 /tmp/gable_build.log; ./build-release/gable_tests | tail -1" || exit 1
ls -la /srv/jammy/src/folio-gable_${V}_amd64.deb || exit 1
cp /srv/jammy/src/folio-gable_${V}_amd64.deb /home/claude/out/
run_audit() {  # root binary env...
  local root=$1 bin=$2; shift 2
  chroot $root bash -c "cd /tmp && env DISPLAY=:96 QT_QPA_PLATFORM=xcb HOME=/tmp/gablehome XDG_CONFIG_HOME=/tmp/gablecfg $* timeout 5400 $bin --audit > /tmp/gable_audit.log 2>&1; grep -E '^FAIL|passed, |peak memory' /tmp/gable_audit.log | tail -25"
}
echo "== audit (source build, heavy lock)"
flock /home/claude/.heavy.lock bash -c "$(declare -f run_audit); run_audit /srv/jammy /src/gable/build-release/folio-gable FOLIO_THEME=light"
echo "== desktop themes (source build)"
for combo in dark:light dark:dark colour:light colour:dark; do
  echo "-- desktop $combo"
  run_audit /srv/jammy /src/gable/build-release/folio-gable GABLE_DESKTOP_THEME=${combo%%:*} FOLIO_THEME=${combo#*:} AUDIT_ONLY=desktoptheme
done
for m in /srv/jammy /srv/nolo /srv/noble; do
  echo "== installed in $m"
  cp /home/claude/out/folio-gable_${V}_amd64.deb $m/tmp/
  flock /home/claude/.dpkg.lock chroot $m bash -c "dpkg -i /tmp/folio-gable_${V}_amd64.deb >/dev/null 2>&1 || apt-get -y -f install >/dev/null 2>&1; dpkg -l folio-gable | tail -1 | awk '{print \$1, \$3}'"
  # the full click-everything walk runs on the source build above; the installed package gets every scenario
  run_audit $m /usr/bin/folio-gable FOLIO_THEME=light AUDIT_QUICK=1
  run_audit $m /usr/bin/folio-gable GABLE_DESKTOP_THEME=colour FOLIO_THEME=light AUDIT_ONLY=desktoptheme
done
echo "== Wayland desktop themes (installed, 24.04)"
for combo in dark:light colour:dark; do
  echo "-- wayland $combo"
  chroot /srv/noble bash -c "cd /tmp && export DISPLAY=:96; HOME=/tmp/gablehome XDG_CONFIG_HOME=/tmp/gablecfg GABLE_DESKTOP_THEME=${combo%%:*} FOLIO_THEME=${combo#*:} AUDIT_ONLY=desktoptheme,dark timeout 1800 /tmp/wl.sh /usr/bin/folio-gable --audit > /tmp/gable_wl.log 2>&1; grep -E '^FAIL|passed, |peak memory' /tmp/gable_wl.log | tail -25"
done
