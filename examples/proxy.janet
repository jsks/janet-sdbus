# Example script using the janet-sdbus proxy API to print the status
# of a loaded systemd unit.
#
# Usage:
#   $ janet proxy.janet <unit>
###

(import sdbus)

(defmacro defn-color
  "Function constructor for ANSI colors"
  [name code]
  ~(def ,name (fn [str] (if (os/isatty) (string/format "\e[%dm%s\e[0m" ,code str)
                          str))))

(defn-color red 31)
(defn-color green 32)
(defn-color yellow 33)
(defn-color white 37)

(defn bold
  "Bold string using ANSI escape codes."
  [str]
  (if (os/isatty) (string/format "\e[1m%s\e[0m" str)
    str))

(defn uuid
  "Given an array of bytes, returns a uuid string."
  [bytes]
  (-> (map |(string/format "%.2x" $) bytes) string/join))

(defn datetime
  "Format microsecond unix time to time/date representation for current locale."
  [us]
  (os/strftime "%c" (/ us 1_000_000) true))

(defn get-unit
  "Returns a sdbus proxy object for a systemd1 service unit."
  [bus name]
  (def service "org.freedesktop.systemd1")
  (def manager (as-> (sdbus/introspect bus service "/org/freedesktop/systemd1") _
                     (sdbus/proxy bus _ :org.freedesktop.systemd1.Manager)))

  (def path (:GetUnit manager name))

  (def unit (as-> (sdbus/introspect bus service path) _
                  (sdbus/proxy bus _ :org.freedesktop.systemd1.Unit))))

(defn service-status
  "Print the partial status of a systemd1 unit."
  [unit]
  (def state (:ActiveState unit))
  (def icon
    (cond (or (= state "active") (= state "activating")) (green "●")
          (or (= state "inactive") (= state "maintenance")) "○"
          (= state "deactivating") (white "○")
          (= state "failed") (red "×")
          (or (= state "refreshing") (= state "reloading")) (green "↻")
          ""))

  (printf "%s %s - %s" icon (:Id unit) (:Description unit))

  (def file-state (:UnitFileState unit))
  (def preset-state (:UnitFilePreset unit))
  (printf "Loaded: %s (%s; %s; preset: %s)" (:LoadState unit) (:FragmentPath unit)
          (if (= file-state "enabled") (bold (green file-state))
            (bold (yellow file-state)))
          (if (= preset-state "enabled") (bold (green preset-state))
            (bold (yellow preset-state))))

  (if (= state "active")
    (do
      (printf "Active: %s (%s) since %s" (bold (green state)) (:SubState unit)
              (datetime (:ActiveEnterTimestamp unit)))
      (printf "Invocation: %s" (uuid (:InvocationID unit))))
    (printf "Active: %s (%s)" state (:SubState unit)))

  (def docs (:Documentation unit))
  (unless (empty? docs)
    (printf "Docs: %s" (string/join docs "\n      "))))

(defn main [& args]
  (when (< (length args) 2)
    (printf "Usage: %s <name>.service" (dyn :current-file))
    (os/exit 1))

  (def name (get args 1))
  (with [bus (sdbus/open-system-bus)]
    (-> (get-unit bus name)
        service-status)))
