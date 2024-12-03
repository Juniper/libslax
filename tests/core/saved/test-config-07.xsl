<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:xutil="http://xml.libslax.org/xutil" version="1.0" extension-element-prefixes="xutil">
  <xsl:template match="/">
    <data>
      <string>
        <xsl:value-of select="xutil:xml-to-slax(., &quot;1.3&quot;)"/>
      </string>
      <unescaped>
        <xsl:value-of select="xutil:xml-to-slax(., &quot;1.3&quot;)" disable-output-escaping="yes"/>
      </unescaped>
    </data>
  </xsl:template>
</xsl:stylesheet>
