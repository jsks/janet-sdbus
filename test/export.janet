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
          :EmitSignal (sdbus/method "" ""
                                    (fn [] (sdbus/emit-signal (dyn :sdbus/bus)
                                                              (dyn :sdbus/path)
                                                              (dyn :sdbus/interface)
                                                              "Signal"
                                                              "o" "/example/path")))

          :Error (sdbus/method "" "" (fn [] (error "Test error")))
          :Mismatch (sdbus/method "" "as" (fn [] 1))
          :Expected (sdbus/method "i" "i" (fn []))
          :Unexpected (sdbus/method "" "" (fn [] "Hello World!"))

          :Constant (sdbus/property "n" 13)
          :MutableWithSignal (sdbus/property "as" @["Hello" "World!"] :ew)
          :MutableNoSignal (sdbus/property "i" 10 :w)
          :Invalidate (sdbus/property "b" true :iw)

          :Signal (sdbus/signal "o")})

(sdbus/request-name bus "org.janet.UnitTests")
(def slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env))

(def spec (sdbus/introspect bus "org.janet.UnitTests" "/org/janet/UnitTests"))
(def proxy (sdbus/proxy bus spec :org.janet.UnitTests))

(def members (get-in spec [:interfaces :org.janet.UnitTests :members]))

###
# Check flags from spec
(assert (deep= (get-in members [:Add :annotations])
               @[{:name "org.freedesktop.DBus.Deprecated" :value "true"}]))

(assert (deep= (get-in members [:Empty :annotations])
               @[{:name "org.freedesktop.DBus.Method.NoReply" :value "true"}]))

(assert (nil? (members :Hidden)))

(assert (= (get-in members [:Constant :access]) "read"))
(assert (= (get-in members [:MutableNoSignal :access]) "readwrite"))
(assert (= (get-in members [:MutableWithSignal :access]) "readwrite"))
(assert (= (get-in members [:Invalidate :access]) "readwrite"))

(assert (deep= (get-in members [:Constant :annotations])
               @[{:name "org.freedesktop.DBus.Property.EmitsChangedSignal" :value "false"}]))

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

(assert (nil? (:Unexpected proxy)))

###
# Properties
(assert (= (:Constant proxy) 13))
(assert (deep= (:MutableWithSignal proxy) @["Hello" "World!"]))
(assert (= (:Invalidate proxy) true))

(assert-error "Read-only property" (:Constant proxy 14))
(assert-error "Invalid type" (:MutableWithSignal proxy true))

###
# Signals
(def ch (:subscribe proxy :Signal))

(sdbus/emit-signal bus "/org/janet/UnitTests" "org.janet.UnitTests"
                   "Signal" "o" "/org/janet/UnitTests")

(def [status msg] (ev/take ch))
(assert (= status :ok))
(assert (= (sdbus/message-read msg) "/org/janet/UnitTests"))

(:EmitSignal proxy)
(def [status msg] (ev/take ch))
(assert (= status :ok))
(assert (= (sdbus/message-read msg) "/example/path"))

(:unsubscribe proxy :Signal)

# PropertyChanged signal w/ value
(:subscribe proxy :PropertiesChanged ch)

(:MutableWithSignal proxy @["Gone"])
(def [status msg] (ev/take ch))
(assert (= status :ok))

(def payload (sdbus/message-read msg :all))
(assert (deep= (:MutableWithSignal proxy) @["Gone"]))
(assert (= (first payload) "org.janet.UnitTests"))
(assert (deep= (get payload 1) @{"MutableWithSignal" ["as" @["Gone"]]}))

# Value unchanged, no signal should be emitted
(:MutableWithSignal proxy @["Gone"])
(assert (zero? (ev/count ch)))

# Value changed for property that should not emit signal
(:MutableNoSignal proxy 42)
(assert (zero? (ev/count ch)))

# PropertyChanged signal w/o value, ie invalidation
(:Invalidate proxy false)
(def [status msg] (ev/take ch))
(assert (= status :ok))

(def payload (sdbus/message-read msg :all))
(assert (= (first payload) "org.janet.UnitTests"))
(assert (deep= (get payload 2) @["Invalidate"]))

# We shouldn't receive additional signals after unsubscribing
(:unsubscribe proxy :PropertiesChanged)
(:MutableWithSignal proxy @["Missed"])

(assert (empty? (proxy :subscriptions)))
(assert (zero? (ev/count ch)))

(assert-error "Unsubscribe from known signal" (:unsubscribe proxy :BogusSignal))

###
# Matches
(def ch (ev/chan))
(sdbus/match-async bus "type='method_call',member='Add',interface='org.janet.UnitTests'" ch)
(:Add proxy 1 2)

(def [status msg] (ev/take ch))
(assert (= status :ok))
(assert (deep= (sdbus/message-read msg :all) @[1 2]))

###
# Misc. error cases
(sdbus/cancel slot)
(assert-error "Removed interface" (:Empty proxy))

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
