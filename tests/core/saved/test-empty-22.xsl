<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:foo="http://example.com/foo" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="foo slax">
  <xsl:template match="/">
    <top>
      <xsl:variable name="x" select="/test"/>
      <xsl:if test="not($x) and not(/foo/bar) and not(goo[zoo and not(moo)])">
        <not xmlns:goo="http://example.com/goo" xsl:extension-element-prefixes="goo">
          <goo:this>oh yeah</goo:this>
        </not>
      </xsl:if>
      <mumble xmlns:zoo="http://example.com/zoo" xmlns:yoo="http://example.com/yoo" xsl:extension-element-prefixes="zoo yoo">
        <zoo:zoo/>
        <yoo:yoo/>
      </mumble>
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
