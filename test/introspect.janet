(use ../vendor/test)
(import sdbus)

(start-suite)

(assert (not (nil? (sdbus/parse-xml "<node></node>"))))
(assert (nil? (sdbus/parse-xml "<interface></interface>")))

(def test-str "<!DOCTYPE>\n<node>\n <interface name=\"org.example\"><method name=\"method\" attr=\"value\"/> </interface></node>\n")

(def result (sdbus/parse-xml test-str))
(def out @[{:attributes {:name "org.example"}
            :tag "interface"
            :children @[{:attributes {:attr "value"
                                      :name "method"}
                         :tag "method"
                         :children @[]}]}])
(assert (deep= result out))

(end-suite)
