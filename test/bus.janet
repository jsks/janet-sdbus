(use ../vendor/test)
(import ../build/sdbus-native :as sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

(assert (sdbus/bus-is-open bus))
(assert (deep= (keys bus) @[:close]))

(def names (sort (sdbus/list-names bus)))
(def msg (sdbus/message-new-method-call
           bus
           "org.freedesktop.DBus"
           "/org/freedesktop/DBus"
           "org.freedesktop.DBus"
           "ListNames"))
(def out (-> (sdbus/call bus msg) sdbus/message-read sort))

(assert (deep= names out))

(:close bus)
(assert (not (sdbus/bus-is-open bus)))

(end-suite)
