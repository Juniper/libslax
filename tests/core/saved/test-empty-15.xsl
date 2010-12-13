<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="slax-test" mvarname="test"/>
  <xsl:variable name="test" select="&quot;global&quot;" mutable="yes" svarname="slax-test"/>
  <xsl:template match="/">
    <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="test" svarname="slax-test" select="&quot;-yes&quot;"/>
    <xsl:variable name="t1" select="$test"/>
    <top>
      <t1>
        <xsl:value-of select="$t1"/>
      </t1>
      <xsl:if test="true()">
        <xsl:variable name="slax-test" mvarname="test"/>
        <xsl:variable name="test" select="&quot;local1&quot;" mutable="yes" svarname="slax-test"/>
        <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="test" svarname="slax-test" select="&quot;-yes&quot;"/>
        <xsl:variable name="t2" select="$test"/>
        <t2>
          <xsl:value-of select="$t2"/>
        </t2>
      </xsl:if>
      <xsl:if test="true()">
        <xsl:variable name="slax-test" mvarname="test"/>
        <xsl:variable name="test" select="&quot;local2&quot;" mutable="yes" svarname="slax-test"/>
        <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="test" svarname="slax-test" select="&quot;-yes&quot;"/>
        <xsl:variable name="t3" select="$test"/>
        <t3>
          <xsl:value-of select="$t3"/>
        </t3>
      </xsl:if>
    </top>
  </xsl:template>
</xsl:stylesheet>
