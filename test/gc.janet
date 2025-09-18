(use ../vendor/test)
(import sdbus)

(start-suite)

(def env {:LongSleep (sdbus/method "" "i" (fn [] (ev/sleep 1) 111))
          :Quick (sdbus/method "" "i" (fn [] 1))})

(defn send-helper [bus &opt method ch]
  (default method "LongSleep")
  (default ch (ev/chan))
  (def msg (sdbus/message-new-method-call bus
                                          "org.janet.UnitTests"
                                          "/org/janet/UnitTests"
                                          "org.janet.UnitTests"
                                          method))
  (sdbus/call-async bus msg ch))

(defmacro setup [&opt method]
  (default method "LongSleep")
  ~(upscope
     (def bus (sdbus/open-user-bus))

     (def request (sdbus/request-name bus "org.janet.UnitTests" :ar))
     (assert (nil? request))

     (def vtable-slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env))

     (def ch (ev/chan))
     (def call-slot (send-helper bus ,method ch))))

###
# Try to trigger segfaults, use-after-free, memory leaks, etc
(do
  (setup)
  (sdbus/close-bus bus)
  (def [status _] (ev/take ch))
  (assert (= status :close))

  # Should be a no-op
  (sdbus/cancel call-slot)
  (sdbus/cancel vtable-slot))

(do
  (setup)
  (sdbus/cancel vtable-slot)
  (def [status _] (ev/take ch))
  (assert (= status :error))
  (sdbus/close-bus bus))

(do
  (setup "Quick")
  (def [status _] (ev/take ch))
  (assert (= status :ok))

  # Cancelling a completed call should be a no-op
  (sdbus/cancel call-slot)

  # As should be additional calls to sdbus/cancel
  (sdbus/cancel call-slot)
  (sdbus/close-bus bus))

(do
  (setup)
  # Cancel a pending call
  (sdbus/cancel call-slot)

  # Canceling twice should be a no-op
  (sdbus/cancel call-slot)

  # Don't close user passed channel. Ideally, we would return write to
  # channel that the call-slot has been canceled; however, we may be
  # in a GC state.
  (assert (ev/count ch) 0)
  (sdbus/cancel vtable-slot)
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

(gccollect)
(end-suite)
