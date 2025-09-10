(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))
(def interface [bus "org.freedesktop.DBus" "/org/freedesktop/DBus" "org.freedesktop.DBus"])

###
# Methods
(assert (string? (sdbus/call-method ;interface "GetId")))

(def name (sdbus/get-unique-name bus))
(def result (sdbus/call-method ;interface "GetConnectionUnixUser" "s" name))
(assert (number? result))

(assert-error "Missing method" (sdbus/call-method ;interface "FakeMethod"))
(assert-error "Missing signature" (sdbus/call-method ;interface "GetConnectionUnixUser"))

###
# Properties
(def interfaces (sdbus/get-property ;interface "Interfaces"))
(assert (tuple? interfaces))
(assert (= (first interfaces) "as"))
(assert (all string? (get interfaces 1)))

(assert-error "Read-only property" (sdbus/set-property ;interface "Interfaces" ["as" @["s" "s"]]))

###
# Signals
(def ch (ev/chan))
(sdbus/subscribe-signal bus "NameOwnerChanged" ch :interface "org.freedesktop.DBus")
(sdbus/call-method ;interface "RequestName" "su" "org.janet.UnitTests" 0)

(def result (ev/take ch))
(assert (= (first result) :ok))
(assert (= (sdbus/message-read (get result 1)) "org.janet.UnitTests"))


(sdbus/close-bus bus)

(end-suite)
