<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:my="http://example.com" xmlns:exslt="http://exslt.org/common" xmlns:slax-func="http://exslt.org/functions" version="1.0" extension-element-prefixes="my exslt slax-func">
  <xsl:template match="/">
    <out>
      <xsl:variable name="a" select="my:test()"/>
      <xsl:variable name="b" select="my:test(1, 2)"/>
      <a>
        <xsl:copy-of select="$a"/>
      </a>
      <b>
        <xsl:copy-of select="$b"/>
      </b>
      <c>
        <xsl:copy-of select="my:one(&quot;test&quot;)"/>
      </c>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="d" select="ext:node-set(my:one(&quot;one&quot;, &quot;two&quot;))"/>
      <d>
        <xsl:value-of select="exslt:object-type($d)"/>
      </d>
    </out>
  </xsl:template>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="my:test">
    <xsl:param name="min" select="0"/>
    <xsl:param name="max" select="100"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions" select="$max - $min"/>
  </slax-func:function>
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="my:one">
    <xsl:param name="name"/>
    <xsl:param name="fish" select="&quot;fish&quot;"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions">
      <name>
        <xsl:value-of select="$name"/>
      </name>
      <fish>
        <xsl:value-of select="$fish"/>
      </fish>
    </slax-func:result>
  </slax-func:function>
</xsl:stylesheet>
