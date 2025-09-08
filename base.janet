# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./native :prefix "" :export false)

(defn call-method
  ```
  Send a method call to a D-Bus service. Suspends the current fiber
  without blocking the event loop.

  If the method expects arguments, the first rest argument must be a
  D-Bus signature string.
  ```
  [bus destination path interface method & rest]
  (def msg (message-new-method-call bus destination path interface method))
  (def signature (first rest))
  (unless (or (nil? signature) (empty? signature))
    (message-append msg signature ;(slice rest 1)))
  (with [ch (ev/chan)]
    (call-async bus msg ch)
    (match (ev/take ch)
      [:ok msg] (message-read-all msg)
      [:error err] (error err)
      result (errorf "Unexpected result: %p" result))))

(defn get-property
  ```
  Get a property from a D-Bus service.
  ```
  [bus destination path interface property]
  (call-method bus destination path
               "org.freedesktop.DBus.Properties" "Get"
               "ss" interface property))

(defn set-property
  ```
  Set a property on a D-Bus service.

  `variant` must a D-Bus variant encoded as a Janet tuple with two
  members: a valid D-Bus signature string and a corresponding value.
  ```
  [bus destination path interface property variant]
  (call-method bus destination path "org.freedesktop.DBus.Properties" "Set"
               "ssv" interface property variant))

(defn emit-signal
  ```
  Emit a D-Bus signal. If the signal expects arguments, the first
  rest argument must be a D-Bus signature string.
  ```
  [bus path interface signal & rest]
  (def msg (message-new-signal bus path interface signal))
  (def signature (first rest))
  (unless (or (nil? signature) (empty? signature))
    (message-append msg signature ;(slice rest 1)))
  (message-send msg))

(defmacro- symbolic-kvs [& args]
  (with-syms [$syms $values]
    ~(let [,$syms ',args
           ,$values [,;args]]
       (keep |(unless (nil? $1) (string/format "%s='%s'" $0 $1)) ,$syms ,$values))))

(defn subscribe-signal
  [bus interface member chan &named path sender argN]
  (default argN [])
  (def rules (symbolic-kvs interface member path sender))
  (def arg-matches (map |(string/format "arg%d='%s'" $0 $1) (pairs argN)))
  (def match-rule (-> (array/concat rules arg-matches)
                      (string/join ",")))
  (match-signal bus match-rule chan))
