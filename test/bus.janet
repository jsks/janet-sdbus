(use ../vendor/test)
(import sdbus/native :as sdbus)

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

(sdbus/close-bus bus)

(assert (not (sdbus/bus-is-open bus)))
(assert-error "Bus is closed" (sdbus/list-names bus))

(end-suite)
