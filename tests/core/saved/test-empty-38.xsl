<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:param name="host" select="&quot;localhost&quot;"/>
  <xsl:template match="/">
    <data>
      <val>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:ends-with(&quot;testing&quot;, &quot;ing&quot;)"/>
      </val>
      <val>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:ends-with(&quot;testing&quot;, &quot;ingg&quot;)"/>
      </val>
      <val>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:ends-with(&quot;testing&quot;, &quot;g&quot;)"/>
      </val>
      <val>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:ends-with(&quot;testing&quot;, &quot;n&quot;)"/>
      </val>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="x" select="slax:get-host($host)"/>
      <localhost>
        <xsl:copy-of select="$x"/>
      </localhost>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="y" select="slax:get-host($x/address[1])"/>
      <one-twenty-seven>
        <xsl:copy-of select="$y/hostname"/>
        <xsl:copy-of select="$y/address-family"/>
        <xsl:copy-of select="$y/address[1]"/>
      </one-twenty-seven>
    </data>
  </xsl:template>
</xsl:stylesheet>
