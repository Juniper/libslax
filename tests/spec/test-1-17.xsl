<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:preserve-space elements="foo goo moo"/>
  <xsl:strip-space elements="boo coo"/>
  <xsl:template match="/">
    <foo>
      <goo>
        <moo>test</moo>
      </goo>
      <boo>
        <coo>test</coo>
      </boo>
    </foo>
  </xsl:template>
</xsl:stylesheet>
