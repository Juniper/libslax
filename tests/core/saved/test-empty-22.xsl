<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:template match="/">
    <top>
      <xsl:variable name="x" select="/test"/>
      <xsl:if test="not($x) and not(/foo/bar) and not(goo[zoo and not(moo)])">
        <not/>
      </xsl:if>
      <xsl:variable name="y" select="10"/>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="z" select="slax:evaluate(&quot;$y + 1&quot;)"/>
      <answer>
        <xsl:copy-of select="$z"/>
      </answer>
      <answer>
        <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:evaluate(&quot;!$x&quot;)"/>
      </answer>
      <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:output(&quot;this &quot;, &quot;is &quot;, &quot;a &quot;, &quot;test&quot;)"/>
    </top>
  </xsl:template>
</xsl:stylesheet>
