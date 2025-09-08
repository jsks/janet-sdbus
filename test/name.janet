(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

(sdbus/request-name bus "org.janet.UnitTests")

# Subsequent requests should no-op
(assert (nil? (sdbus/request-name bus "org.janet.UnitTests")))

(def new-bus (sdbus/open-user-bus))
(assert (= (sdbus/request-name new-bus "org.janet.UnitTests" :a) :queued))

(def ch (ev/chan))
(sdbus/subscribe-signal new-bus "org.freedesktop.DBus" "NameLost" ch
                        :path "/org/freedesktop/DBus" :sender "org.freedesktop.DBus")
(sdbus/subscribe-signal new-bus "org.freedesktop.DBus" "NameAcquired" ch
                        :path "/org/freedesktop/DBus" :sender "org.freedesktop.DBus")

(sdbus/release-name bus "org.janet.UnitTests")

# NameAcquired Signal
(def msg (ev/take ch))
(assert (= (first msg) :ok))
(assert (= (sdbus/message-read (get msg 1)) "org.janet.UnitTests"))

(assert (nil? (sdbus/request-name bus "org.janet.UnitTests" :r)))

# NameLost signal
(def msg (ev/take ch))
(assert (= (first msg) :ok))
(assert (= (sdbus/message-read (get msg 1)) "org.janet.UnitTests"))

(assert-error "Do not queue" (sdbus/request-name new-bus "org.janet.UnitTests" :nr))

(sdbus/close-bus new-bus)
(sdbus/close-bus bus)

(end-suite)
