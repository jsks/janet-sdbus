(use ../vendor/test)
(import sdbus)

(start-suite)

(def bus (sdbus/open-user-bus))

(def interface {:Example (sdbus/method :: "i" -> "i" [x] (+ x 1))
                :NoInput (sdbus/method :: "" -> "s" [] "Hello World!")
                :SideEffects (sdbus/method [])
                :Mismatch (sdbus/method :: "" -> "as" [] 1)
                :Unexpected (sdbus/method :: "" -> "" [] "Hello World!")
                :Error (sdbus/method [] (error "Test error"))
                :DoubleAcc (sdbus/method :: "ad" -> "d" [& args]
                                         (->> (map |(* 2 $) args) (reduce2 +)))})

(sdbus/request-name bus "org.janet.UnitTests")
(def slot (sdbus/export bus "/org/janet/UnitTests" "org.janet.UnitTests" interface))

(def exported-interface [bus "org.janet.UnitTests" "/org/janet/UnitTests" "org.janet.UnitTests"])

(def result (sdbus/call-method ;exported-interface "Example" "i" 10))
(assert (= result 11))

(def result (sdbus/call-method ;exported-interface "DoubleAcc" "ad" @[1 2 3]))
(assert (= result 12))

(def result (sdbus/call-method ;exported-interface "NoInput"))
(assert (= result "Hello World!"))

(def result (sdbus/call-method ;exported-interface "SideEffects"))
(assert (nil? result))

(assert-error "Mismatched signature" (sdbus/call-method ;exported-interface "Mismatch"))
(assert-error "Unexpected output" (sdbus/call-method ;exported-interface "Unexpected"))
(assert-error "Method throw" (sdbus/call-method ;exported-interface "Error"))

(sdbus/cancel slot)
(assert-error "Remove interface" (sdbus/call-method ;exported-interface "NoInput"))

(def service [bus "/org/janet/ErrorCases" "org.janet.ErrorCases"])

(assert-error "Empty interface" (sdbus/export ;service {}))
(assert-error "Missing in-signature" (sdbus/export ;service {:x {:sig-in "s" :fun (fn [])}}))
(assert-error "Missing out-signature" (sdbus/export ;service {:x {:sig-out "s" :fun (fn [])}}))
(assert-error "Missing function" (sdbus/export ;service {:x {:sig-in "s" :sig-out "s"}}))
(assert-error "Invalid method name" (sdbus/export ;service {:!x_2 (sdbus/method [])}))

(sdbus/close-bus bus)

(end-suite)
