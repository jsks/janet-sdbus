(use ../vendor/test)
(import sdbus)

(start-suite)

(assert (not (nil? (sdbus/parse-xml "<node></node>"))))

(def result (sdbus/parse-xml "<node></node>" :destination "org.example" :path "/org/example"))
(assert (= (result :destination) "org.example"))
(assert (= (result :path) "/org/example"))

(def xml
  ```
  <!DOCTYPE>
  <node>
    <interface name="org.example">
      <method name="example-method">
        <annotation name="example-annotation" value="example-value"/>
        <arg name="example-arg" type="i" direction="in"/>
      </method>
      <method name="empty-method"/>
      <property name="example-property" type="as" access="read"/>
      <signal name="example-signal">
        <arg name="signal-arg" type="s"/>
      </signal>
    </interface>
  </node>
  ```)

(def result (sdbus/parse-xml xml))

(assert (empty? (result :destination)))
(assert (= (result :path) "/"))

(def example-method (get-in result [:interfaces :org.example :members :example-method]))
(assert (= (example-method :kind) 'method))
(assert (deep= (example-method :in)
               @[{:name "example-arg" :type "i" :direction "in" :annotations @[]}]))
(assert (deep= (example-method :out) @[]))
(assert (deep= (example-method :annotations)
               @[{:name "example-annotation" :value "example-value"}]))

(def empty-method (get-in result [:interfaces :org.example :members :empty-method]))
(assert (= (empty-method :kind) 'method))
(assert (deep= (empty-method :in) @[]))
(assert (deep= (empty-method :out) @[]))

(def example-property (get-in result [:interfaces :org.example :members :example-property]))
(assert (= (example-property :kind) 'property))
(assert (= (example-property :type) "as"))
(assert (= (example-property :access) "read"))
(assert (deep= (example-property :annotations) @[]))

(def example-signal (get-in result [:interfaces :org.example :members :example-signal]))
(assert (= (example-signal :kind) 'signal))
(assert (deep= (example-signal :args)
               @[{:name "signal-arg" :type "s" :direction "" :annotations @[]}]))
(assert (deep= (example-signal :annotations) @[]))

###
# Malformed input
(assert (nil? (sdbus/parse-xml "<interface></interface>")))
(assert (nil? (sdbus/parse-xml "<node><interface name=\"s\" type=\"i\"/></node>")))

(assert-error "Missing name attribute" (sdbus/parse-xml "<node><interface/></node>"))

(end-suite)
