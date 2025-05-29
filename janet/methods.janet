(import ../sdbus-native)

(defn call-method
  `Send a method call to a D-Bus service`
  [bus destination path interface method signature & args]
  (let [msg (sdbus-native/message-new-method-call bus destination path interface method)]
    (when (not (empty? signature))
      (sdbus-native/message-append msg signature ;args))
    (sdbus-native/call bus msg)))
