# Client-side script for server-client example that consumes the
# published interface, 'org.janet.example'.
#
# Usage
#   $ janet client.janet
###

(import sdbus)

(defn signal-handler [ch]
  (forever
    (match (ev/take ch)
      [:ok msg] (print (sdbus/message-read msg))
      [:error err] (error err)
      [:close _] (break))))

(with [bus (sdbus/open-user-bus)]
  (def store (as-> (sdbus/introspect bus "org.janet.example" "/org/janet/example") _
                    (sdbus/proxy bus _ :org.janet.example)))
  (def ch (ev/chan))
  (:subscribe store :KeyAdded ch)
  (:subscribe store :KeyRemoved ch)

  (ev/spawn (signal-handler ch))

  (:Add store 0 ["s" "Hello World"])
  (:Add store 1 ["o" "/org/janet/example"])
  (:Add store 2 ["i" 10])

  (def [type value] (:Get store 0))
  (assert (= type  "s"))
  (assert (= value "Hello World"))
  (:Remove store 0)

  (printf "Store contains %d items" (:Count store))

  (:Clear store)
  (printf "Store contains %d items" (:Count store)))
