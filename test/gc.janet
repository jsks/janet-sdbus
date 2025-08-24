(use ../vendor/test)
(import sdbus)

(start-suite)

(def env {:LongSleep (sdbus/method :: "" -> "i" [] (ev/sleep 1) 111)})

(defn send-helper [bus &opt ch]
  (default ch (ev/chan))
  (def msg (sdbus/message-new-method-call bus
                                          "org.janet.UnitTests"
                                          "/org/janet/UnitTests"
                                          "org.janet.UnitTests"
                                          "LongSleep"))
  (sdbus/call-async bus msg ch))

(defmacro setup []
  '(upscope
     (def bus (sdbus/open-user-bus))
     (sdbus/request-name bus "org.Janet.UnitTests")
     (def vtable-slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env))
     (def ch (ev/chan))
     (def call-slot (send-helper bus ch))))

###
# Try to trigger segfaults, use-after-free, memory leaks, etc
(var *ch* nil)
(do
  (setup)
  (set *ch* ch))
(gccollect)
(assert (= (ev/count *ch*) 0))

(do
  (setup)
  (sdbus/close-bus bus)
  (def [status _] (ev/take ch))
  (assert (= status :error)))

(do
  (setup)
  (sdbus/cancel vtable-slot)
  (def [status _] (ev/take ch))
  (assert (= status :error)))

(do
  (setup)
  (sdbus/cancel call-slot)

  # Canceling twice should be a no-op
  (sdbus/cancel call-slot)

  # Don't close user passed channel. Ideally, we would return write to
  # channel that the call-slot has been canceled; however, we may be
  # in a GC state.
  (assert (ev/count ch) 0)
  (sdbus/cancel vtable-slot))

(do
  (setup)
  (sdbus/cancel call-slot)
  (sdbus/close-bus bus))


(do
  (def bus (sdbus/open-user-bus))
     (sdbus/request-name bus "org.Janet.UnitTests")
  (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env)
  (send-helper bus)
  (sdbus/close-bus bus))

(do
  (def bus (sdbus/open-user-bus))
  (sdbus/close-bus bus)
  (assert-error "Bus disconnected" (sdbus/call-method bus "org.freedesktop.DBus"
                                                      "/org/freedesktop/DBus"
                                                      "org.freedesktop.DBus"
                                                      "ListNames")))

(end-suite)
