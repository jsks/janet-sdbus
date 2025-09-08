(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))
(def env {:Add (sdbus/method "ii" "i" |(+ $0 $1) :d)
          :DoubleAcc (sdbus/method "ad" "d" (fn [& args] (->> (map |(* 2 $) args)
                                                              (reduce2 +))))
          :Empty (sdbus/method "" "" (fn []) :n)
          :Hidden (sdbus/method "a{is}i" "s" (fn [dict key] (dict key)) :h)
          :Suspend (sdbus/method "" "i" (fn [] (ev/sleep 0.01) 1))
          :ReturnFalse (sdbus/method "i" "b" (fn [x] false))

          :Error (sdbus/method "" "" (fn [] (error "Test error")))
          :Mismatch (sdbus/method "" "as" (fn [] 1))
          :Expected (sdbus/method "i" "i" (fn []))
          :Unexpected (sdbus/method "" "" (fn [] "Hello World!"))

          :Constant (sdbus/property "n" 13)
          :NoSignalMutable (sdbus/property "i" 10 :w)
          :SignalMutable (sdbus/property "as" @["Hello" "World!"] :ew)
          :Invalidate (sdbus/property "b" true :iw)

          :Signal (sdbus/signal "g")})

(sdbus/request-name bus "org.janet.UnitTests")
(def slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env))

(def spec (sdbus/introspect bus "org.janet.UnitTests" "/org/janet/UnitTests"))
(def proxy (sdbus/proxy bus spec :org.janet.UnitTests))

(def members (get-in spec [:interfaces :org.janet.UnitTests :members]))

###
# Check flags from spec
(assert (deep= (get-in members [:Add :annotations])
               @[{:org.freedesktop.DBus.Deprecated "true"}]))

(assert (deep= (get-in members [:Empty :annotations])
               @[{:org.freedesktop.DBus.Method.NoReply "true"}]))

(assert (nil? (members :Hidden)))

(assert (= (get-in members [:Constant :access]) "read"))
(assert (= (get-in members [:NoSignalMutable :access]) "readwrite"))
(assert (= (get-in members [:SignalMutable :access]) "readwrite"))
(assert (= (get-in members [:Invalidate :access]) "readwrite"))

(assert (deep= (get-in members [:Constant :annotations])
               @[{:org.freedesktop.DBus.Property.EmitsChangedSignal "false"}]))

###
# Methods
(assert (= (:Add proxy 1 2) 3))
(assert (= (:DoubleAcc proxy @[1 2 3]) 12))
(assert (nil? (:Empty proxy)))

(def result (sdbus/call-method bus "org.janet.UnitTests"
                               "/org/janet/UnitTests"
                               "org.janet.UnitTests"
                               "Hidden" "a{is}i" @{1 "value"} 1))
(assert (= result "value"))

# Methods yielding to the event-loop should not error
(assert (= (:Suspend proxy) 1))

# Methods returning `false` should not error
(assert (not (:ReturnFalse proxy 10)))

(assert-error "Method throw" (:Error proxy))
(assert-error "Mismatched signature" (:Mismatch proxy))
(assert-error "Expected output" (:Expected proxy))
(assert-error "Unexpected output" (:Unexpected proxy))

###
# Properties
(assert (= (:Constant proxy) 13))
(assert (deep= (:SignalMutable proxy) @["Hello" "World!"]))
(assert (= (:Invalidate proxy) true))

(assert-error "Read-only property" (:Constant proxy 14))
(assert-error "Invalid type" (:SignalMutable proxy true))

###
# Signals
(def ch (:subscribe proxy :Signal))

(sdbus/emit-signal bus "/org/janet/UnitTests"
                   "org.janet.UnitTests"
                   "Signal" "g" "/org/janet/UnitTests")

(def signal (ev/take ch))
(assert (= (first signal) :ok))
(assert (= (sdbus/message-read (get signal 1)) "/org/janet/UnitTests"))

(:unsubscribe proxy :Signal)

# PropertyChanged signal w/ value
(:subscribe proxy :PropertiesChanged ch)

(:SignalMutable proxy @["Gone"])
(def signal-result (ev/take ch))
(def msg (sdbus/message-read-all (get signal-result 1)))

(assert (deep= (:SignalMutable proxy) @["Gone"]))
(assert (= (first signal-result) :ok))
(assert (= (first msg) "org.janet.UnitTests"))
(assert (deep= (get msg 1) @{"SignalMutable" ["as" @["Gone"]]}))

# Value unchanged, no signal should be emitted
(:SignalMutable proxy @["Gone"])
(assert (zero? (ev/count ch)))

# Value changed for property that should not emit signal
(:NoSignalMutable proxy 42)
(assert (zero? (ev/count ch)))

# PropertyChanged signal w/o value, ie invalidation
(:Invalidate proxy false)
(def signal-result (ev/take ch))
(def msg (sdbus/message-read-all (get signal-result 1)))

(assert (= (first signal-result) :ok))
(assert (= (first msg) "org.janet.UnitTests"))
(assert (deep= (get msg 2) @["Invalidate"]))


# We shouldn't receive additional signals after unsubscribing
(:unsubscribe proxy :PropertiesChanged)
(:SignalMutable proxy @["Missed"])

(assert (empty? (proxy :subscriptions)))
(assert (zero? (ev/count ch)))

(assert-error "Unsubscribe from known signal" (:unsubscribe proxy :BogusSignal))

(sdbus/cancel slot)
(assert-error "Removed interface" (:Empty proxy))

###
# Misc. error cases
(def service [bus "/org/janet/ErrorCases" "org.janet.ErrorCases"])

(assert-error "Empty interface" (sdbus/export ;service {}))

(assert-error "Missing in-signature" (sdbus/export ;service {:x {:sig-in "s" :function (fn [])}}))
(assert-error "Missing out-signature" (sdbus/export ;service {:x {:sig-out "s" :function (fn [])}}))
(assert-error "Missing function" (sdbus/export ;service {:x {:sig-in "s" :sig-out "s"}}))
(assert-error "Invalid method name" (sdbus/export ;service {:!x_2 (sdbus/method "" "" (fn []))}))

(assert-error "Invalid method flag" (sdbus/method "" "" (fn []) :x))
(assert-error "Invalid property flag" (sdbus/property "i" 1 :z))
(assert-error "Invalid signal flag" (sdbus/signal "i" :x))

(sdbus/release-name bus "org.janet.UnitTests")
(sdbus/close-bus bus)

(end-suite)
