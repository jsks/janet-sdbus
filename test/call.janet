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

# Test method call timeout
(def msg (sdbus/message-new-method-call ;interface "GetId"))
(with [ch (ev/chan)]
  (sdbus/call-async bus msg ch 5)
  (def [status message] (ev/take ch))
  (assert (= status :error))
  (assert (= (string/has-suffix? "Method call timed out" message))))

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
(def slot (sdbus/subscribe-signal bus "NameOwnerChanged" ch
                                  :interface "org.freedesktop.DBus"))
(sdbus/call-method ;interface "RequestName" "su" "org.janet.UnitTests" 0)

(def [status msg] (ev/take ch))
(assert (= status :ok))
(assert (= (sdbus/message-read msg) "org.janet.UnitTests"))

(sdbus/cancel slot)

###
# Matches
(sdbus/match-async bus "type='method_return',sender='org.freedesktop.DBus'" ch)
(def names (sdbus/call-method ;interface "ListNames"))

(assert (= (ev/count ch) 2))

(def [status _] (ev/take ch))
(assert (= status :ok))

(def [status msg] (ev/take ch))
(assert (= status :ok))
(assert (deep= (sdbus/message-read msg) names))

(sdbus/close-bus bus)

(end-suite)
