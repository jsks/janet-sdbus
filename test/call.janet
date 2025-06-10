(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-system-bus))
(def interface [bus "org.freedesktop.DBus" "/org/freedesktop/DBus" "org.freedesktop.DBus"])

(def get-id (sdbus/method ;interface "GetId"))
(def get-connection-user (sdbus/method ;interface "GetConnectionUnixUser" "s"))

(assert (string? (get-id)))
(assert (number? (get-connection-user (sdbus/get-unique-name bus))))

(def prop:interfaces (sdbus/property ;interface "Interfaces"))

(assert (tuple? (prop:interfaces)))
(assert (= (first (prop:interfaces)) "as"))
(assert (all string? (get (prop:interfaces) 1)))

(end-suite)
