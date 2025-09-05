(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

###
# Test interface export and method calls
(def env {:Example (sdbus/method :: "i" -> "i" [x] (+ x 1))
          :NoInput (sdbus/method :: "" -> "s" [] "Hello World!")
          :SideEffects (sdbus/method [])
          :Mismatch (sdbus/method :: "" -> "as" [] 1)
          :Expected (sdbus/method :: "i" -> "i" [x])
          :Unexpected (sdbus/method [] "Hello World!")
          :Error (sdbus/method [] (error "Test error"))
          :Suspend (sdbus/method :: "" -> "i" [] (ev/sleep 0.01) 1)
          :DoubleAcc (sdbus/method :: "ad" -> "d" [& args]
                                   (->> (map |(* 2 $) args) (reduce2 +)))
          :MultiLine (sdbus/method :: "i" -> "b" [x]
                                   (def y (+ x 2))
                                   false)})

(sdbus/request-name bus "org.janet.UnitTests")
(def slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" env))

(def interface [bus "org.janet.UnitTests" "/org/janet/UnitTests" "org.janet.UnitTests"])

(def result (sdbus/call-method ;interface "Example" "i" 10))
(assert (= result 11))

(def result (sdbus/call-method ;interface "Suspend"))
(assert (= result 1))

(def result (sdbus/call-method ;interface "DoubleAcc" "ad" @[1 2 3]))
(assert (= result 12))

(def result (sdbus/call-method ;interface "MultiLine" "i" 0))
(assert (not result))

(def result (sdbus/call-method ;interface "NoInput"))
(assert (= result "Hello World!"))

(def result (sdbus/call-method ;interface "SideEffects"))
(assert (nil? result))

(assert-error "Mismatched signature" (sdbus/call-method ;interface "Mismatch"))
(assert-error "Expected output" (sdbus/call-method ;interface "Expected"))
(assert-error "Unexpected output" (sdbus/call-method ;interface "Unexpected"))
(assert-error "Method throw" (sdbus/call-method ;interface "Error"))

(sdbus/cancel slot)
(assert-error "Removed interface" (sdbus/call-method ;interface "NoInput"))

###
# Some common error cases
(def service [bus "/org/janet/ErrorCases" "org.janet.ErrorCases"])

(assert-error "Empty interface" (sdbus/export ;service {}))

(assert-error "Missing in-signature" (sdbus/export ;service {:x {:sig-in "s" :function (fn [])}}))
(assert-error "Missing out-signature" (sdbus/export ;service {:x {:sig-out "s" :function (fn [])}}))
(assert-error "Missing function" (sdbus/export ;service {:x {:sig-in "s" :sig-out "s"}}))
(assert-error "Invalid method name" (sdbus/export ;service {:!x_2 (sdbus/method [])}))

###
# Test flags by checking introspection data
(def flag-env {:Example (sdbus/method :: "i" -> "i" :h [x] x)
               :NoReply (sdbus/method :dn [])})

(def flag-interface [bus "org.janet.UnitTests" "/org/janet/UnitTests" "org.janet.UnitTests"])
(def flag-slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" flag-env))

(def spec (sdbus/introspect bus "org.janet.UnitTests" "/org/janet/UnitTests"))
(def members (get-in spec [:interfaces :org.janet.UnitTests :members]))

(assert (= (length members) 1))
(assert (= (first (keys members)) :NoReply))

(def annotations (get-in members [:NoReply :annotations]))
(assert (= (length annotations) 2))
(assert (deep= annotations @[{:org.freedesktop.DBus.Deprecated "true"}
                             {:org.freedesktop.DBus.Method.NoReply "true"}]))

(def flag-interface [bus "org.janet.UnitTests" "/org/janet/UnitTests" "org.janet.UnitTests"])
(assert (= (sdbus/call-method ;flag-interface "Example" "i" 10) 10))
(assert (nil? (sdbus/call-method ;flag-interface "NoReply")))

(sdbus/cancel flag-slot)

(sdbus/release-name bus "org.janet.UnitTests")
(sdbus/close-bus bus)

(end-suite)
