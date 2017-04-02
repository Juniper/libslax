<script>
  <state id="1" action="discard">
    <rule tag="chassis-inventory" action="emit" use-tag="inventory" new-state="2"/>
  </state>
  <state id="2" action="discard">
    <rule tag="chassis" action="discard" new-state="3"/>
  </state>
  <state id="3" action="save">
    <rule tag="name" use-tag="identifier"/>
    <rule tag="serial-number" use-tag="sernum"/>
    <rule tag="description" action="discard"/>
  </state>
</script>
