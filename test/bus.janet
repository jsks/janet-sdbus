(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

(assert (sdbus/bus-is-open? bus))
(assert (string? (sdbus/get-unique-name bus)))
(assert (= (sdbus/get-unique-name bus) (string bus)))
(assert (deep= (keys bus) @[:close]))

(def names (sort (sdbus/list-names bus)))
(def out (-> (sdbus/call-method bus "org.freedesktop.DBus"
                                    "/org/freedesktop/DBus"
                                    "org.freedesktop.DBus"
                                    "ListNames")
             sort))
(assert (deep= names out))

# Sanity check --- by default, local bus connections should support
#  file descriptor sending/receiving
(assert (sdbus/can-send-fds? bus))

(sdbus/close-bus bus)

(assert (not (sdbus/bus-is-open? bus)))
(assert (= (string bus) "closed"))
(assert-error "Bus is closed" (sdbus/list-names bus))

(end-suite)
