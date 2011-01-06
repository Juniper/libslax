<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-func="http://exslt.org/functions" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax-func slax-ext">
  <slax-func:function xmlns:slax-func="http://exslt.org/functions" name="test">
    <xsl:param name="x"/>
    <slax-func:result xmlns:slax-func="http://exslt.org/functions">
      <test>
        <xsl:value-of select="$x"/>
      </test>
    </slax-func:result>
  </slax-func:function>
  <xsl:template match="foo[concat(../../id/text(), &quot;/&quot;, text())]">
    <xsl:variable name="test2" select="&quot;€&quot;"/>
    <xsl:variable name="test" select="&quot;&quot;"/>
    <!-- This one is odd -->
    <xsl:value-of select="concat(&quot;text in &quot;, name(..), ' = &quot;')"/>
    <!-- This one too -->
    <xsl:variable name="date" select="concat(substring(date, 1, 4), &quot;/&quot;, substring(date, 5, 2), &quot;/&quot;, substring(date, 7, 2))"/>
    <!-- This one tickles the bug -->
    <xsl:value-of select="test(concat(local-game-prefix, opponent), answer)"/>
    <!-- This one tickles a second bug -->
    <span class="element">
      <xsl:value-of select="concat(&quot;&lt;/&quot;, name(.), &quot;&gt;&quot;)"/>
    </span>
  </xsl:template>
  <xsl:template match="/">
    <top>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="test" select="slax-ext:node-set(test(&quot;fish&quot;))"/>
      <xsl:variable name="this-temp-1">
        <that>one</that>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="this" select="slax-ext:node-set($this-temp-1)"/>
      <xsl:variable name="mine-temp-2">
        <your>was</your>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="mine" select="slax-ext:node-set($mine-temp-2)"/>
      <xsl:variable name="gray-temp-3">
        <new>black</new>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="gray" select="slax-ext:node-set($gray-temp-3)"/>
      <xsl:copy-of select="$this"/>
      <xsl:copy-of select="$that"/>
      <xsl:copy-of select="$gray"/>
    </top>
  </xsl:template>
</xsl:stylesheet>
