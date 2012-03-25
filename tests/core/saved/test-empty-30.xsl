<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:func="http://exslt.org/functions" xmlns:test-ns="http://xml.juniper.net/test" version="1.0">
  <xsl:template match="/">
    <test>
      <xsl:value-of select="test-ns:test-func()"/>
    </test>
  </xsl:template>
  <func:function name="test-ns:test-func">
    <xsl:value-of select="concat(&quot;Between &quot;, $x)"/>
  </func:function>
</xsl:stylesheet>
