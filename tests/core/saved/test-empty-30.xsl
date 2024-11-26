<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:func="http://exslt.org/functions" xmlns:test-ns="http://xml.juniper.net/test" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="slax-func">
  <xsl:template match="/">
    <test>
      <xsl:value-of select="test-ns:test-func(0)"/>
    </test>
  </xsl:template>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="test-ns:test-func">
    <xsl:param name="x"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions" select="concat(&quot;Between &quot;, $x)"/>
  </slax-func:function>
</xsl:stylesheet>
