(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-system-bus))
(def interface [bus "org.freedesktop.DBus" "/org/freedesktop/DBus" "org.freedesktop.DBus"])

(def get-id (sdbus/remote-method ;interface "GetId"))
(assert (string? (get-id)))

(def get-connection-user (sdbus/remote-method ;interface "GetConnectionUnixUser" "s"))
(assert (number? (get-connection-user (sdbus/get-unique-name bus))))

(def prop:interfaces (sdbus/remote-property ;interface "Interfaces"))
(assert (tuple? (prop:interfaces)))
(assert (= (first (prop:interfaces)) "as"))
(assert (all string? (get (prop:interfaces) 1)))

(assert-error "Missing method" (sdbus/call-method ;interface "FakeMethod"))
(assert-error "Missing signature" (sdbus/call-method ;interface "GetConnectionUnixUser"))

(sdbus/close-bus bus)

(end-suite)
