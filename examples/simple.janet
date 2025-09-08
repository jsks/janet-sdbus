# Example script that queries systemd-hostnamed for system
# information.
#
# Usage:
#   $ janet simple.janet
###

(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def interface ["org.freedesktop.hostname1"
                  "/org/freedesktop/hostname1"
                  "org.freedesktop.hostname1"])

  (def [_ hostname] (sdbus/get-property bus ;interface "Hostname"))
  (def [_ os] (sdbus/get-property bus ;interface "OperatingSystemPrettyName"))

  (printf "Host: %s" hostname)
  (printf "Operating System: %s" os))
