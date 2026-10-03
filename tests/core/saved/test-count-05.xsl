<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="last" select="document(&quot;test-count.xml&quot;)"/>
  <xsl:variable name="one" select="$last/in" mutable="yes"/>
  <xsl:variable name="two" select="$last/in/node()" mutable="yes"/>
  <xsl:template match="/">
    <top>
      <one>
        <before>
          <xsl:copy-of select="$one"/>
        </before>
        <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="one">
          <new>one</new>
        </slax:append-to-variable>
        <after>
          <xsl:copy-of select="$one"/>
        </after>
      </one>
      <two>
        <before>
          <xsl:copy-of select="$two"/>
        </before>
        <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="two">
          <new>one</new>
        </slax:append-to-variable>
        <after>
          <xsl:copy-of select="$two"/>
        </after>
      </two>
    </top>
  </xsl:template>
</xsl:stylesheet>
